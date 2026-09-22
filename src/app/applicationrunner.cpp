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

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <cstdio>
#include <optional>

#include "configuration.h"
#include "logger.h"
#include "mainwindow.h"
#include "windowchrome.h"
#include "persistentinfo.h"
#include "storagebootstrapdialog.h"
#include "storagelocator.h"
#include "styles.h"

#include "cli.h"
#include "kloggapp.h"
#include "applicationlanguage.h"
#include "applicationsmokepaths.h"
#include "storagebootstrap.h"
#include "zzlogg_brand.h"
#include "zzloggapplicationidentity.h"

namespace {

enum class UiSmokeStage {
    PrepareWindows,
    WaitForDocuments,
    VerifyFirstTitle,
    VerifySecondTitle,
    WaitForSearch,
    WaitForTheme,
};

void reportUiSmokeSetupFailure( const QString& error )
{
    const QByteArray diagnostic
        = QStringLiteral( "UI smoke setup failure: %1\n" ).arg( error ).toLocal8Bit();
    std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
    std::fflush( stderr );
}

void reportStorageBootstrapFailure( const QString& error, bool showDialog )
{
    const QByteArray diagnostic
        = QStringLiteral( "ZzLogg storage bootstrap failure: %1\n" ).arg( error ).toLocal8Bit();
    std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
    std::fflush( stderr );
    if ( showDialog ) {
        QMessageBox::critical(
            nullptr, QApplication::applicationDisplayName(),
            QApplication::translate( "ApplicationRunner",
                                     "ZzLogg could not open its data directory:\n%1" )
                .arg( error ) );
    }
}

void reportStartupGuardFailure( const QString& message, const QString& detail, bool showDialog )
{
    const QString diagnostic = detail.isEmpty() ? message : message + "\n" + detail;
    const QByteArray utf8
        = QStringLiteral( "ZzLogg startup guard failure: %1\n" ).arg( diagnostic ).toLocal8Bit();
    std::fwrite( utf8.constData(), 1, static_cast<size_t>( utf8.size() ), stderr );
    std::fflush( stderr );
    if ( showDialog ) {
        QMessageBox::critical( nullptr, QApplication::applicationDisplayName(), diagnostic );
    }
}

struct UiSmokeState {
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
    UiSmokeStage stage = UiSmokeStage::PrepareWindows;
};

void finishUiSmoke( UiSmokeState& state, int exitCode, const QString& message = {} )
{
    if ( !message.isEmpty() ) {
        const QByteArray diagnostic
            = QStringLiteral( "UI smoke failure: %1\n" ).arg( message ).toLocal8Bit();
        std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
        std::fflush( stderr );
        LOG_ERROR << "UI smoke failure: " << message;
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
    if ( !window->windowChrome() || window->windowChrome()->usesNativeFallback() ) {
        *error = QStringLiteral( "Fluent window chrome was not constructed" );
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

void pollUiSmoke( const std::shared_ptr<UiSmokeState>& state )
{
    if ( state->elapsed.elapsed() >= state->deadlineMs ) {
        finishUiSmoke( *state, EXIT_FAILURE,
                        QStringLiteral( "timed out waiting for %1" ).arg( state->waitingFor ) );
        return;
    }

    switch ( state->stage ) {
    case UiSmokeStage::PrepareWindows: {
        if ( !state->app->property( "zzlogg.fluentUi" ).toBool() ) {
            finishUiSmoke( *state, EXIT_FAILURE, QStringLiteral( "Fluent UI runtime fell back" ) );
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
            finishUiSmoke( *state, EXIT_FAILURE,
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
                    finishUiSmoke( *state, EXIT_FAILURE,
                                    QStringLiteral( "restored window: %1" ).arg( error ) );
                    return;
                }
            }
            finishUiSmoke( *state, EXIT_SUCCESS );
            return;
        }

        state->secondWindow = state->app->newWindow();
        state->secondTitleBar = validateFluentWindow( state->secondWindow, &error );
        if ( state->secondTitleBar == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "new window before first show: %1" ).arg( error ) );
            return;
        }
        state->secondWindow->show();

        if ( state->mode == QStringLiteral( "seed-session" ) ) {
            const auto snapshot = state->app->mainWindows();
            if ( snapshot.size() < 2
                 || !QMetaObject::invokeMethod( state->firstWindow, "exitRequested",
                                                Qt::DirectConnection ) ) {
                finishUiSmoke( *state, EXIT_FAILURE,
                                QStringLiteral( "could not enter session-saving close path" ) );
                return;
            }
            finishUiSmoke( *state, EXIT_SUCCESS );
            return;
        }

        state->documentTabs
            = state->firstWindow->findChild<QTabWidget*>( QStringLiteral( "documentTabs" ) );
        if ( state->documentTabs == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE, QStringLiteral( "documentTabs is missing" ) );
            return;
        }
        state->waitingFor = QStringLiteral( "two document tabs" );
        state->stage = UiSmokeStage::WaitForDocuments;
        return;
    }
    case UiSmokeStage::WaitForDocuments:
        if ( state->documentTabs == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "documentTabs disappeared while waiting for documents" ) );
            return;
        }
        if ( state->documentTabs->count() != 2 ) {
            return;
        }
        state->documentTabs->setCurrentIndex( 0 );
        state->waitingFor = QStringLiteral( "the first document title" );
        state->stage = UiSmokeStage::VerifyFirstTitle;
        return;
    case UiSmokeStage::VerifyFirstTitle:
        if ( state->firstWindow == nullptr || state->documentTabs == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "window or documentTabs disappeared before first title" ) );
            return;
        }
        if ( !hasExpectedDocumentTitle( *state->firstWindow, QStringLiteral( "ui-first.log" ) ) ) {
            return;
        }
        state->documentTabs->setCurrentIndex( 1 );
        state->waitingFor = QStringLiteral( "the second document title" );
        state->stage = UiSmokeStage::VerifySecondTitle;
        return;
    case UiSmokeStage::VerifySecondTitle: {
        if ( state->firstWindow == nullptr || state->documentTabs == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "window or documentTabs disappeared before second title" ) );
            return;
        }
        if ( !hasExpectedDocumentTitle( *state->firstWindow,
                                        QStringLiteral( "ui-second.log" ) ) ) {
            return;
        }
        QWidget* const crawler = state->documentTabs->currentWidget();
        if ( crawler == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE, QStringLiteral( "current crawler is missing" ) );
            return;
        }
        state->searchEdit = crawler->findChild<QComboBox*>( QStringLiteral( "mainSearchEdit" ) );
        state->searchButton
            = crawler->findChild<QToolButton*>( QStringLiteral( "mainSearchButton" ) );
        state->filteredResultsTabs
            = crawler->findChild<QTabWidget*>( QStringLiteral( "filteredResultsTabs" ) );
        if ( state->searchEdit == nullptr || state->searchButton == nullptr
             || state->filteredResultsTabs == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE, QStringLiteral( "search controls are missing" ) );
            return;
        }
        state->searchEdit->setEditText( QStringLiteral( "ERROR" ) );
        state->searchButton->click();
        if ( state->searchButton->isVisible() ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "search did not enter the asynchronous state" ) );
            return;
        }
        if ( state->mode == QStringLiteral( "destroy-search-edit" ) ) {
            state->searchEdit->deleteLater();
        }
        else if ( state->mode == QStringLiteral( "close-second-window-during-search" ) ) {
            if ( state->secondWindow == nullptr ) {
                finishUiSmoke(
                    *state, EXIT_FAILURE,
                    QStringLiteral( "second window disappeared before close regression" ) );
                return;
            }
            state->secondWindow->setAttribute( Qt::WA_DeleteOnClose );
            state->secondWindow->close();
        }
        state->waitingFor = QStringLiteral( "the search to finish" );
        state->stage = UiSmokeStage::WaitForSearch;
        return;
    }
    case UiSmokeStage::WaitForSearch: {
        if ( state->firstTitleBar == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "first title bar disappeared while waiting for search" ) );
            return;
        }
        if ( state->secondTitleBar == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "second title bar disappeared while waiting for search" ) );
            return;
        }
        if ( state->searchEdit == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "mainSearchEdit disappeared while waiting for search" ) );
            return;
        }
        if ( state->searchButton == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "mainSearchButton disappeared while waiting for search" ) );
            return;
        }
        if ( state->filteredResultsTabs == nullptr ) {
            finishUiSmoke(
                *state, EXIT_FAILURE,
                QStringLiteral( "filteredResultsTabs disappeared while waiting for search" ) );
            return;
        }
        if ( !state->searchButton->isVisible() ) {
            return;
        }
        if ( state->searchEdit->currentText() != QStringLiteral( "ERROR" )
             || state->filteredResultsTabs->count() < 1 ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "search result contract was not preserved" ) );
            return;
        }
        auto* const themeButton = state->firstTitleBar->findChild<QToolButton*>(
            QStringLiteral( "zzTitleBarThemeButton" ) );
        if ( themeButton == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "theme toggle button is missing" ) );
            return;
        }
        if ( state->firstTitleBar->property( "themeMode" ).toInt()
                 != static_cast<int>( UiThemeMode::Light )
             || state->secondTitleBar->property( "themeMode" ).toInt()
                    != static_cast<int>( UiThemeMode::Light ) ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "theme did not begin in Light mode" ) );
            return;
        }
        themeButton->click();
        state->waitingFor = QStringLiteral( "the Dark theme to synchronize" );
        state->stage = UiSmokeStage::WaitForTheme;
        return;
    }
    case UiSmokeStage::WaitForTheme: {
        if ( state->firstTitleBar == nullptr || state->secondTitleBar == nullptr ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "title bar disappeared while waiting for theme" ) );
            return;
        }
        const QVariant firstTheme = state->firstTitleBar->property( "themeMode" );
        const QVariant secondTheme = state->secondTitleBar->property( "themeMode" );
        if ( Configuration::get().uiThemeMode() != UiThemeMode::Dark || firstTheme != secondTheme
             || firstTheme.toInt() != static_cast<int>( UiThemeMode::Dark ) ) {
            return;
        }
        auto& settings = PersistentInfo::getSettings( app_settings{} );
        settings.sync();
        if ( settings.status() != QSettings::NoError
             || settings.value( QStringLiteral( "view.themeMode" ) ).toString()
                    != QStringLiteral( "dark" ) ) {
            finishUiSmoke( *state, EXIT_FAILURE,
                            QStringLiteral( "theme toggle did not persist Dark mode" ) );
            return;
        }
        finishUiSmoke( *state, EXIT_SUCCESS );
        return;
    }
    }
}

void startUiSmokeProbe( KloggApp& app, int deadlineMs, QString mode )
{
    app.setQuitOnLastWindowClosed( false );
    auto state = std::make_shared<UiSmokeState>();
    state->app = &app;
    state->deadlineMs = deadlineMs;
    state->mode = std::move( mode );
    state->timer = new QTimer( &app );
    state->timer->setInterval( 50 );
    state->elapsed.start();
    QObject::connect( state->timer, &QTimer::timeout, &app, [ state ] { pollUiSmoke( state ); } );
    state->timer->start();
}

void startUiManualIsolationDeadline( KloggApp& app, int deadlineMs )
{
    QTimer::singleShot( deadlineMs, &app, [ &app ] {
        reportUiSmokeSetupFailure(
            QStringLiteral( "manual isolation deadline expired" ) );
        app.exit( EXIT_FAILURE );
    } );
}

void setApplicationAttributes()
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

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );
}

} // namespace

int runKloggApplication( int argc, char* argv[], KloggApplicationOptions options )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_process_init();
#endif

    prepareZzLoggApplicationIdentity();
    setApplicationAttributes();
    KloggApp app{ argc, argv };
    CliParameters parameters{ app };

    const auto startupPlan = planApplicationSmokeStartup(
        qEnvironmentVariable( "ZZLOGG_UI_SMOKE_MS" ),
        qEnvironmentVariable( "ZZLOGG_UI_SMOKE_MODE" ),
        static_cast<bool>( options.createUiRuntime ) );
    const ApplicationSmokeRequest& uiSmoke = startupPlan.smokeRequest;
    const bool showStorageBootstrapFailureDialog
        = shouldShowStorageBootstrapFailureDialog( uiSmoke.requested, uiSmoke.mode );

    // The pre-bootstrap language install is read-only; do it before any
    // startup guard diagnostics so failures are understandable in the GUI.
    const QString bootstrapLanguage = preBootstrapLanguage( QLocale::system() );
    if ( MainWindow::installLanguage( bootstrapLanguage ) != 0 ) {
        reportStorageBootstrapFailure(
            QStringLiteral( "failed to install pre-bootstrap language: %1" )
                .arg( bootstrapLanguage ),
            showStorageBootstrapFailureDialog );
        return EXIT_FAILURE;
    }

    // Enter the installation activity lease before the storage selector on the
    // primary path. Short-lived secondary forwarding processes must not hold a
    // directory lease or they block the primary instance's update reservation.
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    if ( !parameters.multi_instance && app.isSecondary() ) {
        const auto guardStatus
            = ApplicationUpdateGuard::probeStartupWithoutLease( applicationDirectory );
        if ( guardStatus == ApplicationUpdateGuard::Status::Blocked
             || guardStatus == ApplicationUpdateGuard::Status::Unavailable ) {
            const QString message
                = guardStatus == ApplicationUpdateGuard::Status::Blocked
                      ? QApplication::translate( "ApplicationRunner",
                                                 "This ZzLogg installation is currently being updated. "
                                                 "Start ZzLogg again after the update has finished." )
                      : QApplication::translate( "ApplicationRunner",
                                                 "ZzLogg could not verify its installation directory. "
                                                 "Startup was cancelled to protect the installation." );
            reportStartupGuardFailure( message, {}, showStorageBootstrapFailureDialog );
            return EXIT_FAILURE;
        }
        app.sendFilesToPrimaryInstance( parameters.filenames );
        return app.exec();
    }

    const auto guardStatus = app.updateGuard().enter( applicationDirectory );
    if ( guardStatus == ApplicationUpdateGuard::Status::Blocked
         || guardStatus == ApplicationUpdateGuard::Status::Unavailable ) {
        const QString message
            = guardStatus == ApplicationUpdateGuard::Status::Blocked
                  ? QApplication::translate( "ApplicationRunner",
                                             "This ZzLogg installation is currently being updated. "
                                             "Start ZzLogg again after the update has finished." )
                  : QApplication::translate( "ApplicationRunner",
                                             "ZzLogg could not verify its installation directory. "
                                             "Startup was cancelled to protect the installation." );
        reportStartupGuardFailure( message, app.updateGuard().errorText(),
                                   showStorageBootstrapFailureDialog );
        return EXIT_FAILURE;
    }

    QString iconError;
    if ( !applyZzLoggApplicationIcon( app, &iconError ) ) {
        const QByteArray diagnostic
            = QStringLiteral( "%1: resource %2\n" )
                  .arg( iconError, QString::fromLatin1( zzlogg::brand::IconResource ) )
                  .toLocal8Bit();
        std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
        std::fflush( stderr );
        return EXIT_FAILURE;
    }

    const auto smokeStoragePaths = resolveApplicationSmokeStoragePaths(
        uiSmoke.requested,
        qEnvironmentVariable( "ZZLOGG_UI_SMOKE_APP_CONFIG_DIR" ),
        qEnvironmentVariable( "ZZLOGG_UI_SMOKE_USER_DATA_DIR" ), [] {
            const QSettings legacySettings{
                QSettings::IniFormat, QSettings::UserScope,
                QString::fromLatin1( zzlogg::brand::SettingsOrganization ),
                QString::fromLatin1( zzlogg::brand::SettingsApplication )
            };
            return ApplicationSmokeStoragePaths{
                QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ),
                QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ),
                QFileInfo{ legacySettings.fileName() }.absolutePath(), {}, false };
        } );
    const QString appConfigDirectory = smokeStoragePaths.appConfigDirectory;
    const QString userDataDirectory = smokeStoragePaths.userDataDirectory;
    const QString legacyUserSettingsDirectory
        = smokeStoragePaths.legacyUserSettingsDirectory;
    const auto storageResult = bootstrapStorage(
        applicationDirectory, appConfigDirectory, userDataDirectory,
        legacyUserSettingsDirectory,
        parameters.data_dir,
        []( const StorageBootstrapPrompt& prompt ) -> std::optional<StorageLocation> {
            StorageBootstrapDialog dialog;
            dialog.configurePaths( prompt.applicationDirectory, prompt.userDataDirectory );
            return dialog.exec() == QDialog::Accepted ? dialog.selectedLocation() : std::nullopt;
        } );
    if ( storageResult.status != StorageBootstrapStatus::Ready ) {
        if ( storageResult.status == StorageBootstrapStatus::Error ) {
            reportStorageBootstrapFailure( storageResult.error,
                                           showStorageBootstrapFailureDialog );
        }
        return storageResult.status == StorageBootstrapStatus::Cancelled ? EXIT_SUCCESS
                                                                         : EXIT_FAILURE;
    }

    const auto& config = Configuration::getSynced();
    const StorageLocation& currentLocation = StorageContext::current().location();
    if ( !currentLocation.commandLineOverride && !currentLocation.locatorPath.isEmpty() ) {
        QString locatorError;
        const StorageLocatorStore locatorStore{ applicationDirectory, appConfigDirectory };
        if ( !locatorStore.writeActive( currentLocation, &locatorError ) ) {
            reportStorageBootstrapFailure(
                QStringLiteral( "failed to verify storage locator %1 for %2: %3" )
                    .arg( currentLocation.locatorPath, currentLocation.dataRoot, locatorError ),
                showStorageBootstrapFailureDialog );
            return EXIT_FAILURE;
        }
    }

    if ( uiSmoke.requested && !isManualIsolationSmokeMode( uiSmoke.mode ) ) {
        auto& smokeConfiguration = Configuration::get();
        smokeConfiguration.setUiThemeMode( UiThemeMode::Light );
        smokeConfiguration.save();
    }
    if ( MainWindow::installLanguage( config.language() ) != 0 ) {
        reportStorageBootstrapFailure(
            QStringLiteral( "failed to install configured language: %1" ).arg( config.language() ),
            showStorageBootstrapFailureDialog );
        return EXIT_FAILURE;
    }

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config.loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config.enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config.enableLogging(), logLevel );


    auto maxConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );

    LOG_INFO << zzlogg::brand::ProductName << " instance"
             << ", mimalloc v" << mi_version() << ", default concurrency " << maxConcurrency;

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

    if ( startupPlan.createUiRuntime ) {
        uiRuntime = options.createUiRuntime( app, &runtimeError );
    }
    if ( !uiRuntime ) {
        StyleManager::applyStyle( config.style() );
    }

    auto startNewSession = true;
    MainWindow* mw = nullptr;
    if ( parameters.load_session
         || ( parameters.filenames.empty() && !parameters.new_session
              && config.loadLastSession() ) ) {
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

    if ( uiSmoke.requested ) {
        if ( isManualIsolationSmokeMode( uiSmoke.mode ) ) {
            startUiManualIsolationDeadline( app, uiSmoke.deadlineMs );
        }
        else {
            startUiSmokeProbe( app, uiSmoke.deadlineMs, uiSmoke.mode );
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
