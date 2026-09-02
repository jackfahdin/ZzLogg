#include "configuration.h"
#include "abstractlogview.h"
#include "kloggapp.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "storagecontext.h"
#include "tabbedcrawlerwidget.h"
#include "zzloggfluentshell.h"
#include "zzloggapplicationidentity.h"
#include "zzlogg_brand.h"
#include "zzlogguiruntime.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPalette>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

class RuntimeContractTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void decoratesRealWindowsAndRoutesSemanticState()
    {
        auto& app = *qobject_cast<KloggApp*>( qApp );
        QCOMPARE( app.applicationName(), QStringLiteral( "ZzLogg" ) );
        QCOMPARE( app.applicationDisplayName(), QStringLiteral( "ZzLogg" ) );
        QCOMPARE( app.organizationName(), QStringLiteral( "JackfahdinQt" ) );
        const QUrl homepageUrl(
            QString::fromLatin1( zzlogg::brand::HomepageUrl ) );
        QVERIFY( homepageUrl.isValid() );
        QVERIFY( !homepageUrl.host().isEmpty() );
        QCOMPARE( app.organizationDomain(), homepageUrl.host() );
        QVERIFY( !app.windowIcon().isNull() );
        auto& configuration = Configuration::getSynced();
        configuration.setUiThemeMode( UiThemeMode::Dark );
        configuration.save();
        auto& settings = PersistentInfo::getSettings( app_settings{} );
        settings.sync();
        const QString settingsPath = QDir::fromNativeSeparators( settings.fileName() );
        QVERIFY2( settingsPath.startsWith( QDir::fromNativeSeparators(
                      qEnvironmentVariable( "ZZLOGG_TEST_SETTINGS_ROOT" ) ) ),
                  qPrintable( settingsPath ) );
        QVERIFY( QFileInfo::exists( settings.fileName() ) );
        QString error;
        auto runtime = ZzLoggUiRuntime::create( app, &error );
        QVERIFY2( runtime, qPrintable( error ) );
        QVERIFY( app.property( "zzlogg.fluentUi" ).toBool() );
        auto* style = qobject_cast<ZzFluentUI::ZzFluentStyle*>( app.style() );
        QVERIFY( style );

        int warningCount = 0;
        auto failureSession = std::make_shared<Session>();
        MainWindow failureWindow(
            WindowSession( failureSession, QStringLiteral( "runtime-install-failure" ), 0 ) );
        auto* const customMenuWidget = new QWidget( &failureWindow );
        failureWindow.setMenuWidget( customMenuWidget );
        runtime->decorate( failureWindow );
        QTimer::singleShot( 0, &app, [ &warningCount ] {
            for ( QWidget* widget : QApplication::topLevelWidgets() ) {
                if ( auto* warning = qobject_cast<QMessageBox*>( widget ) ) {
                    ++warningCount;
                    warning->accept();
                }
            }
        } );
        QCoreApplication::processEvents();
        QCOMPARE( warningCount, 1 );
        QCOMPARE( failureWindow.menuWidget(), customMenuWidget );
        QVERIFY( !failureWindow.property( "zzlogg.fluentShellInstalled" ).toBool() );
        QVERIFY(
            !failureWindow.findChild<ZzLoggFluentShell*>( QString(), Qt::FindDirectChildrenOnly ) );

        MainWindow* const first = app.newWindow();
        MainWindow* const second = app.newWindow();
        QVERIFY( !first->windowIcon().isNull() );
        QCOMPARE( first->windowIcon().cacheKey(), app.windowIcon().cacheKey() );
        QVERIFY( first->findChild<ZzLoggFluentShell*>( QStringLiteral( "zzloggFluentShell" ) ) );
        auto* const secondShell
            = second->findChild<ZzLoggFluentShell*>( QStringLiteral( "zzloggFluentShell" ) );
        QVERIFY( secondShell );

        QTemporaryDir logDirectory;
        QVERIFY( logDirectory.isValid() );
        QFile logFile( logDirectory.filePath( QStringLiteral( "theme-propagation.log" ) ) );
        QVERIFY( logFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QVERIFY( logFile.write( "first visible log line\nsecond visible log line\n"
                                "third visible log line\n" ) > 0 );
        logFile.close();

        first->resize( 960, 720 );
        first->loadFileNonInteractive( logFile.fileName() );
        auto* const documentTabs
            = first->findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
        QVERIFY( documentTabs );
        QTRY_COMPARE_WITH_TIMEOUT( documentTabs->count(), 1, 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( first->findChild<AbstractLogView*>() != nullptr, 5000 );
        auto* const logView = first->findChild<AbstractLogView*>();
        QVERIFY( logView );
        QTRY_VERIFY_WITH_TIMEOUT( logView->viewport()->isVisible()
                                      && logView->viewport()->width() > 100
                                      && logView->viewport()->height() > 100,
                                  5000 );

        QSignalSpy documentSpy( first, &MainWindow::activeDocumentNameChanged );
        Q_EMIT first->activeDocumentNameChanged( {} );
        QCOMPARE( first->windowTitle(), QStringLiteral( "ZzLogg" ) );
        Q_EMIT first->activeDocumentNameChanged( QStringLiteral( "server.log" ) );
        QCOMPARE( first->windowTitle(), QStringLiteral( "server.log \u2014 ZzLogg" ) );
        QCOMPARE( documentSpy.count(), 2 );

        const QDateTime settingsMtime = QFileInfo( settings.fileName() ).lastModified();
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );
        Q_EMIT first->uiThemeChanged( UiThemeMode::Light );
        QTRY_COMPARE_WITH_TIMEOUT( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Light,
                                   5000 );
        settings.sync();
        QCOMPARE( Configuration::get().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( QFileInfo( settings.fileName() ).lastModified(), settingsMtime );
        QCOMPARE( app.palette().color( QPalette::Base ), QColor( QStringLiteral( "#ffffff" ) ) );
        QTRY_COMPARE_WITH_TIMEOUT( logView->viewport()->palette().color( QPalette::Base ),
                                   QColor( QStringLiteral( "#ffffff" ) ), 5000 );
        const QImage renderedViewport = logView->viewport()->grab().toImage();
        QVERIFY( !renderedViewport.isNull() );
        const QPoint blankContentPoint( renderedViewport.width() * 3 / 4,
                                        renderedViewport.height() * 3 / 4 );
        QVERIFY( renderedViewport.rect().contains( blankContentPoint ) );
        QVERIFY( renderedViewport.pixelColor( blankContentPoint ).lightness() > 200 );
        QVERIFY( !documentTabs->testAttribute( Qt::WA_StyleSheet ) );
        Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::HighContrast );
        QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::System );
        Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::Dark );
        QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

        first->close();
        second->close();
        QCoreApplication::processEvents();
        runtime.reset();
        QVERIFY( qobject_cast<ZzFluentUI::ZzFluentStyle*>( app.style() ) == nullptr );
    }
};

int main( int argc, char* argv[] )
{
    QTemporaryDir settingsRoot;
    if ( !settingsRoot.isValid() )
        return 3;
    const QString appData = settingsRoot.filePath( QStringLiteral( "appdata" ) );
    const QString localAppData = settingsRoot.filePath( QStringLiteral( "localappdata" ) );
    const QString xdgConfig = settingsRoot.filePath( QStringLiteral( "xdg-config" ) );
    QDir().mkpath( appData );
    QDir().mkpath( localAppData );
    QDir().mkpath( xdgConfig );
    qputenv( "APPDATA", QDir::toNativeSeparators( appData ).toLocal8Bit() );
    qputenv( "LOCALAPPDATA", QDir::toNativeSeparators( localAppData ).toLocal8Bit() );
    qputenv( "XDG_CONFIG_HOME", xdgConfig.toLocal8Bit() );
    qputenv( "ZZLOGG_TEST_SETTINGS_ROOT", settingsRoot.path().toLocal8Bit() );
    QStandardPaths::setTestModeEnabled( true );
    const auto prepared = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if ( !prepared )
        return 2;
    prepareZzLoggApplicationIdentity();
    KloggApp app( argc, argv );
    if ( !StorageContext::install(
             { StorageMode::CustomDirectory, settingsRoot.path(),
               settingsRoot.filePath( QStringLiteral( "storage.ini" ) ), true } ) ) {
        return 5;
    }
    QString iconError;
    if ( !applyZzLoggApplicationIcon( app, &iconError ) ) {
        qCritical().noquote() << iconError;
        return 4;
    }
    RuntimeContractTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "runtimecontracttest.moc"
