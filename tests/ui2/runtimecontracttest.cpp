#include "configuration.h"
#include "kloggapp.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "zzloggfluentshell.h"
#include "zzlogguiruntime.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

const bool PersistentInfo::ForcePortable = false;

class RuntimeContractTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void decoratesRealWindowsAndRoutesSemanticState()
    {
        auto& app = *qobject_cast<KloggApp*>( qApp );
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
        QVERIFY( first->findChild<ZzLoggFluentShell*>( QStringLiteral( "zzloggFluentShell" ) ) );
        auto* const secondShell
            = second->findChild<ZzLoggFluentShell*>( QStringLiteral( "zzloggFluentShell" ) );
        QVERIFY( secondShell );

        QSignalSpy documentSpy( first, &MainWindow::activeDocumentNameChanged );
        Q_EMIT first->activeDocumentNameChanged( {} );
        QCOMPARE( first->windowTitle(), QStringLiteral( "ZzLogg" ) );
        Q_EMIT first->activeDocumentNameChanged( QStringLiteral( "server.log" ) );
        QCOMPARE( first->windowTitle(), QStringLiteral( "server.log \u2014 ZzLogg" ) );
        QCOMPARE( documentSpy.count(), 2 );

        const QDateTime settingsMtime = QFileInfo( settings.fileName() ).lastModified();
        QTest::qWait( 20 );
        Q_EMIT first->uiThemeChanged( UiThemeMode::Light );
        settings.sync();
        QCOMPARE( Configuration::get().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( QFileInfo( settings.fileName() ).lastModified(), settingsMtime );
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Light );
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
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settingsRoot.path() );
    const auto prepared = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if ( !prepared )
        return 2;
    KloggApp app( argc, argv );
    RuntimeContractTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "runtimecontracttest.moc"
