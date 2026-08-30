/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "applicationrunner.h"

#include "log.h"
#include <QtGlobal>
#include <qapplication.h>
#include <qthreadpool.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

#include <mimalloc.h>
#include <roaring.hh>

#ifdef KLOGG_HAS_HS
#include <hs.h>
#endif

#include "tbb/global_control.h"

#include <QMessageBox>
#include <QAction>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <cstdio>
#include <optional>

#include "configuration.h"
#include "logger.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "styles.h"

#include "cli.h"
#include "kloggapp.h"

namespace {

enum class Ui2SmokeStage {
    PrepareWindows,
    WaitForDocuments,
    VerifyFirstTitle,
    VerifySecondTitle,
    WaitForSearch,
    WaitForTheme,
};

struct Ui2SmokeRequest {
    bool requested = false;
    int deadlineMs = 0;
    QString mode;
    QString error;
};

Ui2SmokeRequest prepareUi2SmokeRequest( const KloggApplicationOptions& options )
{
    Ui2SmokeRequest request;
    if ( !options.createUiRuntime ) {
        return request;
    }

    bool validDeadline = false;
    request.deadlineMs
        = qEnvironmentVariableIntValue( "ZZLOGG_UI2_SMOKE_MS", &validDeadline );
    request.requested = validDeadline && request.deadlineMs > 0;
    if ( !request.requested ) {
        return request;
    }

    request.mode = qEnvironmentVariable( "ZZLOGG_UI2_SMOKE_MODE" );
#ifdef Q_OS_WIN
    const QString settingsRoot = qEnvironmentVariable( "APPDATA" );
    const QString settingsRootVariable = QStringLiteral( "APPDATA" );
#else
    const QString settingsRoot = qEnvironmentVariable( "XDG_CONFIG_HOME" );
    const QString settingsRootVariable = QStringLiteral( "XDG_CONFIG_HOME" );
#endif
    if ( settingsRoot.isEmpty() ) {
        request.error = QStringLiteral( "UI2 smoke settings root is missing: %1 is empty" )
                            .arg( settingsRootVariable );
        return request;
    }

    const auto normalizedSettingsRoot = QDir::fromNativeSeparators( settingsRoot );
    if ( !setPersistentSettingsOverrideForProcess( QSettings::IniFormat,
                                                   normalizedSettingsRoot ) ) {
        request.error = QStringLiteral(
                            "UI2 smoke settings override was rejected because PersistentInfo "
                            "was already initialized or another process override was installed; "
                            "format=IniFormat; path=%1" )
                            .arg( normalizedSettingsRoot );
        return request;
    }
    return request;
}

void reportUi2SmokeSetupFailure( const QString& error )
{
    const QByteArray diagnostic
        = QStringLiteral( "UI2 smoke setup failure: %1\n" ).arg( error ).toLocal8Bit();
    std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
    std::fflush( stderr );
}

struct Ui2SmokeState {
    KloggApp* app = nullptr;
    QPointer<QTimer> timer;
    QPointer<MainWindow> firstWindow;
    QPointer<MainWindow> secondWindow;
    QPointer<QTabWidget> documentTabs;
    QPointer<QComboBox> searchEdit;
    QPointer<QToolButton> searchButton;
    QPointer<QTabWidget> filteredResultsTabs;
    QPointer<QWidget> firstTitleBar;
    QPointer<QWidget> secondTitleBar;
    QElapsedTimer elapsed;
    QString mode;
    QString waitingFor = QStringLiteral( "the first window" );
    int deadlineMs = 0;
    Ui2SmokeStage stage = Ui2SmokeStage::PrepareWindows;
};

void finishUi2Smoke( Ui2SmokeState& state, int exitCode, const QString& message = {} )
{
    if ( !message.isEmpty() ) {
        const QByteArray diagnostic
            = QStringLiteral( "UI2 smoke failure: %1\n" ).arg( message ).toLocal8Bit();
        std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
        std::fflush( stderr );
        LOG_ERROR << "UI2 smoke failure: " << message;
    }
    if ( state.timer ) {
        state.timer->stop();
    }
    state.app->exit( exitCode );
}

QWidget* validateFluentWindow( MainWindow* window, QString* error )
{
    if ( window == nullptr ) {
        *error = QStringLiteral( "main window is missing" );
        return nullptr;
    }
    if ( !window->property( "zzlogg.fluentShellInstalled" ).toBool() ) {
        *error = QStringLiteral( "Fluent shell was not installed" );
        return nullptr;
    }
    if ( window->centralWidget() == nullptr ) {
        *error = QStringLiteral( "main window central widget is missing" );
        return nullptr;
    }
    auto* const titleBar = window->findChild<QWidget*>( QStringLiteral( "zzloggFluentTitleBar" ) );
    if ( titleBar == nullptr ) {
        *error = QStringLiteral( "Fluent title bar is missing" );
        return nullptr;
    }
    return titleBar;
}

bool hasExpectedDocumentTitle( const MainWindow& window, const QString& fileName )
{
    return window.windowTitle().contains( fileName )
           && window.windowTitle().endsWith( QStringLiteral( " \u2014 ZzLogg" ) );
}

void pollUi2Smoke( const std::shared_ptr<Ui2SmokeState>& state )
{
    if ( state->elapsed.elapsed() >= state->deadlineMs ) {
        finishUi2Smoke( *state, EXIT_FAILURE,
                        QStringLiteral( "timed out waiting for %1" ).arg( state->waitingFor ) );
        return;
    }

    switch ( state->stage ) {
    case Ui2SmokeStage::PrepareWindows: {
        if ( !state->app->property( "zzlogg.fluentUi" ).toBool() ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "Fluent UI runtime fell back" ) );
            return;
        }

        const auto windows = state->app->mainWindows();
        if ( windows.isEmpty() ) {
            return;
        }

        state->firstWindow = windows.front();
        QString error;
        state->firstTitleBar = validateFluentWindow( state->firstWindow, &error );
        if ( state->firstTitleBar == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "first window: %1" ).arg( error ) );
            return;
        }

        if ( state->mode == QStringLiteral( "verify-restored" ) ) {
            if ( windows.size() < 2 ) {
                state->waitingFor = QStringLiteral( "at least two restored windows" );
                return;
            }
            for ( MainWindow* window : windows ) {
                if ( validateFluentWindow( window, &error ) == nullptr ) {
                    finishUi2Smoke( *state, EXIT_FAILURE,
                                    QStringLiteral( "restored window: %1" ).arg( error ) );
                    return;
                }
            }
            finishUi2Smoke( *state, EXIT_SUCCESS );
            return;
        }

        state->secondWindow = state->app->newWindow();
        state->secondTitleBar = validateFluentWindow( state->secondWindow, &error );
        if ( state->secondTitleBar == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "new window before first show: %1" ).arg( error ) );
            return;
        }
        state->secondWindow->show();

        if ( state->mode == QStringLiteral( "seed-session" ) ) {
            const auto snapshot = state->app->mainWindows();
            if ( snapshot.size() < 2
                 || !QMetaObject::invokeMethod( state->firstWindow, "exitRequested",
                                                Qt::DirectConnection ) ) {
                finishUi2Smoke( *state, EXIT_FAILURE,
                                QStringLiteral( "could not enter session-saving close path" ) );
                return;
            }
            finishUi2Smoke( *state, EXIT_SUCCESS );
            return;
        }

        state->documentTabs = state->firstWindow->findChild<QTabWidget*>(
            QStringLiteral( "documentTabs" ) );
        if ( state->documentTabs == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "documentTabs is missing" ) );
            return;
        }
        state->waitingFor = QStringLiteral( "two document tabs" );
        state->stage = Ui2SmokeStage::WaitForDocuments;
        return;
    }
    case Ui2SmokeStage::WaitForDocuments:
        if ( state->documentTabs == nullptr ) {
            finishUi2Smoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "documentTabs disappeared while waiting for documents" ) );
            return;
        }
        if ( state->documentTabs->count() != 2 ) {
            return;
        }
        state->documentTabs->setCurrentIndex( 0 );
        state->waitingFor = QStringLiteral( "the first document title" );
        state->stage = Ui2SmokeStage::VerifyFirstTitle;
        return;
    case Ui2SmokeStage::VerifyFirstTitle:
        if ( state->firstWindow == nullptr || state->documentTabs == nullptr ) {
            finishUi2Smoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "window or documentTabs disappeared before first title" ) );
            return;
        }
        if ( !hasExpectedDocumentTitle( *state->firstWindow,
                                        QStringLiteral( "ui2-first.log" ) ) ) {
            return;
        }
        state->documentTabs->setCurrentIndex( 1 );
        state->waitingFor = QStringLiteral( "the second document title" );
        state->stage = Ui2SmokeStage::VerifySecondTitle;
        return;
    case Ui2SmokeStage::VerifySecondTitle: {
        if ( state->firstWindow == nullptr || state->documentTabs == nullptr ) {
            finishUi2Smoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "window or documentTabs disappeared before second title" ) );
            return;
        }
        if ( !hasExpectedDocumentTitle( *state->firstWindow,
                                        QStringLiteral( "ui2-second.log" ) ) ) {
            return;
        }
        QWidget* const crawler = state->documentTabs->currentWidget();
        if ( crawler == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "current crawler is missing" ) );
            return;
        }
        state->searchEdit
            = crawler->findChild<QComboBox*>( QStringLiteral( "mainSearchEdit" ) );
        state->searchButton
            = crawler->findChild<QToolButton*>( QStringLiteral( "mainSearchButton" ) );
        state->filteredResultsTabs
            = crawler->findChild<QTabWidget*>( QStringLiteral( "filteredResultsTabs" ) );
        if ( state->searchEdit == nullptr || state->searchButton == nullptr
             || state->filteredResultsTabs == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "search controls are missing" ) );
            return;
        }
        state->searchEdit->setEditText( QStringLiteral( "ERROR" ) );
        state->searchButton->click();
        if ( state->searchButton->isVisible() ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "search did not enter the asynchronous state" ) );
            return;
        }
        if ( state->mode == QStringLiteral( "destroy-search-edit" ) ) {
            state->searchEdit->deleteLater();
        }
        else if ( state->mode == QStringLiteral( "close-second-window-during-search" ) ) {
            if ( state->secondWindow == nullptr ) {
                finishUi2Smoke(
                    *state, EXIT_FAILURE,
                    QStringLiteral( "second window disappeared before close regression" ) );
                return;
            }
            state->secondWindow->setAttribute( Qt::WA_DeleteOnClose );
            state->secondWindow->close();
        }
        state->waitingFor = QStringLiteral( "the search to finish" );
        state->stage = Ui2SmokeStage::WaitForSearch;
        return;
    }
    case Ui2SmokeStage::WaitForSearch: {
        if ( state->firstTitleBar == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "first title bar disappeared while waiting for search" ) );
            return;
        }
        if ( state->secondTitleBar == nullptr ) {
            finishUi2Smoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "second title bar disappeared while waiting for search" ) );
            return;
        }
        if ( state->searchEdit == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "mainSearchEdit disappeared while waiting for search" ) );
            return;
        }
        if ( state->searchButton == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "mainSearchButton disappeared while waiting for search" ) );
            return;
        }
        if ( state->filteredResultsTabs == nullptr ) {
            finishUi2Smoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "filteredResultsTabs disappeared while waiting for search" ) );
            return;
        }
        if ( !state->searchButton->isVisible() ) {
            return;
        }
        if ( state->searchEdit->currentText() != QStringLiteral( "ERROR" )
             || state->filteredResultsTabs->count() < 1 ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "search result contract was not preserved" ) );
            return;
        }
        auto* const themeMenu = state->firstTitleBar->findChild<QMenu*>(
            QStringLiteral( "zzTitleBarThemeMenu" ) );
        if ( themeMenu == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "theme menu is missing" ) );
            return;
        }
        QAction* darkAction = nullptr;
        for ( QAction* action : themeMenu->actions() ) {
            if ( action->data().toInt() == static_cast<int>( UiThemeMode::Dark ) ) {
                darkAction = action;
                break;
            }
        }
        if ( darkAction == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "Dark theme action is missing" ) );
            return;
        }
        if ( state->firstTitleBar->property( "themeMode" ).toInt()
                 != static_cast<int>( UiThemeMode::System )
             || state->secondTitleBar->property( "themeMode" ).toInt()
                    != static_cast<int>( UiThemeMode::System ) ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "theme did not begin in System mode" ) );
            return;
        }
        darkAction->trigger();
        state->waitingFor = QStringLiteral( "the Dark theme to synchronize" );
        state->stage = Ui2SmokeStage::WaitForTheme;
        return;
    }
    case Ui2SmokeStage::WaitForTheme: {
        if ( state->firstTitleBar == nullptr || state->secondTitleBar == nullptr ) {
            finishUi2Smoke( *state, EXIT_FAILURE,
                            QStringLiteral( "title bar disappeared while waiting for theme" ) );
            return;
        }
        const QVariant firstTheme = state->firstTitleBar->property( "themeMode" );
        const QVariant secondTheme = state->secondTitleBar->property( "themeMode" );
        if ( Configuration::get().uiThemeMode() != UiThemeMode::Dark
             || firstTheme != secondTheme
             || firstTheme.toInt() != static_cast<int>( UiThemeMode::Dark ) ) {
            return;
        }
        finishUi2Smoke( *state, EXIT_SUCCESS );
        return;
    }
    }
}

void startUi2SmokeProbe( KloggApp& app, int deadlineMs, QString mode )
{
    app.setQuitOnLastWindowClosed( false );
    auto state = std::make_shared<Ui2SmokeState>();
    state->app = &app;
    state->deadlineMs = deadlineMs;
    state->mode = std::move( mode );
    state->timer = new QTimer( &app );
    state->timer->setInterval( 50 );
    state->elapsed.start();
    QObject::connect( state->timer, &QTimer::timeout, &app,
                      [ state ] { pollUi2Smoke( state ); } );
    state->timer->start();
}

void setApplicationAttributes( bool enableQtHdpi, int scaleFactorRounding )
{
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // all network interfaces to see if anything changes and if so, what. This
    // creates a latency spike every 10 seconds on Mac OS 10.12+ and Windows 7 >=
    // when on a wifi connection.
    // So here we disable it for lack of better measure.
    // This will also cause this message: QObject::startTimer: Timers cannot
    // have negative intervals
    // For more info see:
    // - https://bugreports.qt.io/browse/QTBUG-40332
    // - https://bugreports.qt.io/browse/QTBUG-46015
    qputenv( "QT_BEARER_POLL_TIMEOUT", QByteArray::number( std::numeric_limits<int>::max() ) );

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute( Qt::AA_DisableWindowContextHelpButton );
#endif

    if ( !enableQtHdpi ) {
        QCoreApplication::setAttribute( Qt::AA_DisableHighDpiScaling );
    }
    else {

#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            static_cast<Qt::HighDpiScaleFactorRoundingPolicy>( scaleFactorRounding ) );
#else
        Q_UNUSED( scaleFactorRounding );
#endif

        // This attribute must be set before QGuiApplication is constructed:
        QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling );
        // We support high-dpi (aka Retina) displays
        QCoreApplication::setAttribute( Qt::AA_UseHighDpiPixmaps );
    }
#else
    Q_UNUSED( enableQtHdpi );
    Q_UNUSED( scaleFactorRounding );
#endif

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );
}

} // namespace

int runKloggApplication( int argc, char* argv[], KloggApplicationOptions options )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_process_init();
#endif

    const Ui2SmokeRequest ui2Smoke = prepareUi2SmokeRequest( options );
    if ( ui2Smoke.requested ) {
        QString setupError = ui2Smoke.error;
        const QString portableConfigPath = kloggPortableConfigPath();
        if ( setupError.isEmpty() && QFileInfo::exists( portableConfigPath ) ) {
            setupError = QStringLiteral( "portable configuration is not allowed: %1" )
                             .arg( portableConfigPath );
        }
        if ( !setupError.isEmpty() ) {
            reportUi2SmokeSetupFailure( setupError );
            return EXIT_FAILURE;
        }
    }

    std::optional<KloggApp> appStorage;
    const Configuration* config = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
    if ( ui2Smoke.requested ) {
        setApplicationAttributes( true, 0 );
        appStorage.emplace( argc, argv );
        config = &Configuration::getSynced();
    }
    else
#endif
    {
        config = &Configuration::getSynced();
        setApplicationAttributes( config->enableQtHighDpi(), config->scaleFactorRounding() );
        appStorage.emplace( argc, argv );
    }
    auto& app = *appStorage;
    if ( ui2Smoke.requested ) {
        auto& smokeConfiguration = Configuration::get();
        smokeConfiguration.setUiThemeMode( UiThemeMode::System );
        smokeConfiguration.save();
    }
    MainWindow::installLanguage( config->language() );
    CliParameters parameters( app );

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config->loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config->enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config->enableLogging(), logLevel );

    app.initCrashHandler();

    auto maxConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );

    LOG_INFO << "Klogg instance"
             << ", mimalloc v" << mi_version()
             << ", default concurrency " << maxConcurrency;

    roaring_memory_t roaring_memory_allocators;
    roaring_memory_allocators.malloc = mi_malloc;
    roaring_memory_allocators.realloc = mi_realloc;
    roaring_memory_allocators.calloc = mi_calloc;
    roaring_memory_allocators.free = mi_free;
    roaring_memory_allocators.aligned_malloc = mi_aligned_alloc;
    roaring_memory_allocators.aligned_free = mi_free;
    roaring_init_memory_hook( roaring_memory_allocators );

#ifdef KLOGG_HAS_HS
    hs_set_allocator( mi_malloc, mi_free );
#endif

    if ( maxConcurrency < 2 ) {
        maxConcurrency = 2;
        LOG_INFO << "Overriding default concurrency to " << maxConcurrency;
        tbb::global_control concurrencyControl( tbb::global_control::max_allowed_parallelism,
                                                maxConcurrency );
        QThreadPool::globalInstance()->setMaxThreadCount( static_cast<int>( maxConcurrency ) );
    }

    std::unique_ptr<QObject> uiRuntime;
    QString runtimeError;

    if ( !parameters.multi_instance && app.isSecondary() ) {
        LOG_INFO << "Found another klogg, pid " << app.primaryPid();
        app.sendFilesToPrimaryInstance( parameters.filenames );
    }
    else {
        if ( options.createUiRuntime ) {
            uiRuntime = options.createUiRuntime( app, &runtimeError );
        }
        if ( !uiRuntime ) {
            StyleManager::applyStyle( config->style() );
        }

        auto startNewSession = true;
        MainWindow* mw = nullptr;
        if ( parameters.load_session
             || ( parameters.filenames.empty() && !parameters.new_session
                  && config->loadLastSession() ) ) {
            mw = app.reloadSession();
            startNewSession = false;
        }
        else {
            mw = app.newWindow();
            mw->reloadGeometry();
            mw->show();
        }

        if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
            mw->resize( parameters.window_width, parameters.window_height );
        }

        for ( const auto& filename : parameters.filenames ) {
            mw->loadInitialFile( filename, parameters.follow_file );
        }

        if ( startNewSession ) {
            app.clearInactiveSessions();
        }

        app.startBackgroundTasks();

        if ( ui2Smoke.requested ) {
            startUi2SmokeProbe( app, ui2Smoke.deadlineMs, ui2Smoke.mode );
        }
    }

    auto warning = options.startupWarning;
    if ( !runtimeError.isEmpty() ) {
        if ( !warning.isEmpty() ) {
            warning.append( "\n\n" );
        }
        warning.append( runtimeError );
    }
    if ( !warning.isEmpty() ) {
        LOG_ERROR << "ZzLogg UI startup warning: " << warning;
        QTimer::singleShot( 0, &app, [ warning ] {
            QMessageBox::warning( nullptr, QObject::tr( "ZzLogg UI" ), warning );
        } );
    }

    return app.exec();
}
