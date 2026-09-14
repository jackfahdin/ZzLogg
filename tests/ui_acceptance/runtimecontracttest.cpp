#include "configuration.h"
#include "abstractlogview.h"
#include "crawlerwidget.h"
#include "infoline.h"
#include "kloggapp.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "storagecontext.h"
#include "tabbedcrawlerwidget.h"
#include "windowchrome.h"
#include "zzloggapplicationidentity.h"
#include "zzlogg_brand.h"
#include "uiruntime.h"
#include <algorithm>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGradient>
#include <QMessageBox>
#include <QPalette>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QtTest>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

class RuntimeContractTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void closesAndDestroysWindowBeforeRuntimeShutdown()
    {
        auto& app = *qobject_cast<KloggApp*>(qApp);
        Configuration::getSynced().setMinimizeToTray(false);
        QString error;
        auto runtime = UiRuntime::create(app, &error);
        QVERIFY2(runtime, qPrintable(error));
        QPointer<MainWindow> window = app.newWindow();
        QPointer<WindowChrome> chrome = window->windowChrome();
        window->show();
        QVERIFY(window->closeForApplicationExit());
        QVERIFY(app.mainWindows().isEmpty());
        runtime.reset();
        QVERIFY(window.isNull());
        QVERIFY(chrome.isNull());
        QVERIFY(!app.property("zzlogg.fluentUi").toBool());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void tearsDownWindowsBeforeThemeAndClearsFactory()
    {
        auto& app = *qobject_cast<KloggApp*>(qApp);
        Configuration::getSynced();
        QString error;
        auto runtime = UiRuntime::create(app, &error);
        QVERIFY2(runtime, qPrintable(error));
        QPointer<MainWindow> window = app.newWindow();
        QPointer<WindowChrome> chrome = window->windowChrome();
        QPointer<QWidget> title = window->menuWidget();
        QVERIFY(chrome);
        QVERIFY(title);
        runtime.reset();
        QVERIFY(window.isNull());
        QVERIFY(chrome.isNull());
        QVERIFY(title.isNull());
        QVERIFY(app.mainWindows().isEmpty());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* native = app.newWindow();
        QVERIFY(!native->windowChrome());
        app.destroyMainWindows();
    }
    void migratesLegacySystemToExplicitMode()
    {
        auto& app = *qobject_cast<KloggApp*>( qApp );
        auto& configuration = Configuration::getSynced();
        configuration.setUiThemeMode( UiThemeMode::System );
        configuration.save();

        QString error;
        auto runtime = UiRuntime::create( app, &error );
        QVERIFY2( runtime, qPrintable( error ) );
        auto* style = qobject_cast<ZzFluentUI::ZzFluentStyle*>( app.style() );
        QVERIFY( style );

        const auto resolvedMode = style->themeSnapshot()->mode();
        QVERIFY( resolvedMode == ZzFluentUI::ZzThemeMode::Light
                 || resolvedMode == ZzFluentUI::ZzThemeMode::Dark );
        const auto expected = resolvedMode == ZzFluentUI::ZzThemeMode::Dark
            ? UiThemeMode::Dark
            : UiThemeMode::Light;
        QCOMPARE( Configuration::getSynced().uiThemeMode(), expected );
        QCOMPARE( uiThemeModeStorageValue( expected ),
                  expected == UiThemeMode::Dark ? QStringLiteral( "dark" )
                                                : QStringLiteral( "light" ) );
        auto& settings = PersistentInfo::getSettings( app_settings{} );
        settings.sync();
        QCOMPARE( settings.value( QStringLiteral( "view.themeMode" ) ).toString(),
                  expected == UiThemeMode::Dark ? QStringLiteral( "dark" )
                                                : QStringLiteral( "light" ) );
        runtime.reset();
    }

    void constructsRealWindowsAndRoutesSemanticState()
    {
        auto& app = *qobject_cast<KloggApp*>( qApp );
        QCOMPARE( app.applicationName(), QStringLiteral( "ZzLogg" ) );
        QCOMPARE( app.applicationDisplayName(), QStringLiteral( "ZzLogg" ) );
        // Qt uses this namespace for the storage locator and default data root.
        // Keep it stable even when the displayed company name changes.
        QCOMPARE( app.organizationName(), QStringLiteral( "JackfahdinQt" ) );
        QCOMPARE( QString::fromLatin1( zzlogg::brand::Vendor ), QStringLiteral( "Jackfahdin" ) );
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
        auto runtime = UiRuntime::create( app, &error );
        QVERIFY2( runtime, qPrintable( error ) );
        QVERIFY( app.property( "zzlogg.fluentUi" ).toBool() );
        auto* style = qobject_cast<ZzFluentUI::ZzFluentStyle*>( app.style() );
        QVERIFY( style );

        MainWindow* const first = app.newWindow();
        MainWindow* const second = app.newWindow();
        QVERIFY( !first->windowIcon().isNull() );
        QCOMPARE( first->windowIcon().cacheKey(), app.windowIcon().cacheKey() );
        QVERIFY( first->windowChrome() );
        auto* const secondShell
            = second->windowChrome();
        QVERIFY( secondShell );

        auto* const mainToolBar = first->findChild<QToolBar*>();
        QVERIFY( mainToolBar );
        const auto toolBarActions = mainToolBar->actions();
        QVERIFY( toolBarActions.size() >= 4 );
        const QList<QAction*> themeSensitiveToolBarActions = toolBarActions.mid( 0, 4 );
        const auto iconLightness = []( const QIcon& icon ) {
            const QImage image
                = icon.pixmap( QSize( 16, 16 ), QIcon::Normal, QIcon::Off ).toImage();
            int lightnessSum = 0;
            int visiblePixelCount = 0;
            for ( int y = 0; y < image.height(); ++y ) {
                for ( int x = 0; x < image.width(); ++x ) {
                    const QColor color = image.pixelColor( x, y );
                    if ( color.alpha() > 10 ) {
                        lightnessSum += color.lightness();
                        ++visiblePixelCount;
                    }
                }
            }
            return visiblePixelCount == 0 ? -1 : lightnessSum / visiblePixelCount;
        };
        const auto toolBarIconsMatchTheme = [ &themeSensitiveToolBarActions,
                                              &iconLightness ]( bool darkTheme ) {
            return std::all_of(
                themeSensitiveToolBarActions.cbegin(), themeSensitiveToolBarActions.cend(),
                [ &iconLightness, darkTheme ]( const QAction* action ) {
                    const int lightness = iconLightness( action->icon() );
                    return darkTheme ? lightness > 200 : lightness >= 0 && lightness < 80;
                } );
        };
        QVERIFY( toolBarIconsMatchTheme( true ) );

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
        auto* const documentTabBar = documentTabs->findChild<CrawlerTabBar*>();
        QVERIFY( documentTabBar );
        QVERIFY( documentTabBar->styleSheet().isEmpty() );
        QVERIFY( !documentTabBar->testAttribute( Qt::WA_StyleSheet ) );
        QVERIFY( !documentTabs->testAttribute( Qt::WA_StyleSheet ) );
        auto* const crawler = qobject_cast<CrawlerWidget*>( documentTabs->widget( 0 ) );
        QVERIFY( crawler );
        auto* const searchButton
            = crawler->findChild<QToolButton*>( QStringLiteral( "mainSearchButton" ) );
        QVERIFY( searchButton );
        QVERIFY( iconLightness( searchButton->icon() ) > 200 );
        auto* const searchInfoLine = crawler->findChild<InfoLine*>();
        QVERIFY( searchInfoLine );
        QTRY_VERIFY_WITH_TIMEOUT( first->findChild<AbstractLogView*>() != nullptr, 5000 );
        auto* const logView = first->findChild<AbstractLogView*>();
        QVERIFY( logView );
        QTRY_VERIFY_WITH_TIMEOUT( logView->viewport()->isVisible()
                                      && logView->viewport()->width() > 100
                                      && logView->viewport()->height() > 100,
                                  5000 );

        QTRY_VERIFY_WITH_TIMEOUT(
            logView->viewport()->palette().color( QPalette::Base ).lightness() < 100, 5000 );
        const int stableScrollBarWidth = logView->verticalScrollBar()->width();
        QVERIFY( stableScrollBarWidth > 0 );
        logView->verticalScrollBar()->setFixedWidth( stableScrollBarWidth );
        logView->updateDisplaySize();
        QCoreApplication::processEvents();

        const QPalette darkGaugeBasePalette = searchInfoLine->palette();
        QVERIFY( darkGaugeBasePalette.color( QPalette::Window ).lightness() < 100 );
        searchInfoLine->displayGauge( 37 );
        searchInfoLine->show();
        QTRY_VERIFY_WITH_TIMEOUT( searchInfoLine->isVisible(), 5000 );
        const auto gaugeWindowStop = [ searchInfoLine ] {
            const QBrush background
                = searchInfoLine->palette().brush( searchInfoLine->backgroundRole() );
            const QGradient* const gradient = background.gradient();
            if ( gradient == nullptr || gradient->type() != QGradient::LinearGradient ) {
                return QColor{};
            }
            const QGradientStops stops = gradient->stops();
            return stops.isEmpty() ? QColor{} : stops.constLast().second;
        };
        const QColor darkGaugeWindowStop = gaugeWindowStop();
        QVERIFY( darkGaugeWindowStop.isValid() );
        QCOMPARE( darkGaugeWindowStop, darkGaugeBasePalette.color( QPalette::Window ) );

        logView->followSet( true );
        QCoreApplication::processEvents();
        const QSize darkViewportSize = logView->viewport()->size();
        const int darkHorizontalPageStep = logView->horizontalScrollBar()->pageStep();
        const QImage darkRenderedViewport = logView->viewport()->grab().toImage();
        QVERIFY( !darkRenderedViewport.isNull() );
        const QPoint blankContentPoint( darkRenderedViewport.width() * 3 / 4,
                                        darkRenderedViewport.height() * 3 / 4 );
        QVERIFY( darkRenderedViewport.rect().contains( blankContentPoint ) );
        const QColor darkBlankContent = darkRenderedViewport.pixelColor( blankContentPoint );
        QVERIFY2( darkBlankContent.lightness() < 100,
                  qPrintable( QStringLiteral( "dark content sample was %1" )
                                  .arg( darkBlankContent.name( QColor::HexArgb ) ) ) );
        const QPoint pullToFollowBackgroundPoint( 10, darkRenderedViewport.height() - 5 );
        QVERIFY( darkRenderedViewport.rect().contains( pullToFollowBackgroundPoint ) );
        const QColor darkPullToFollowBackground
            = darkRenderedViewport.pixelColor( pullToFollowBackgroundPoint );
        QVERIFY2( darkPullToFollowBackground.lightness() < 100,
                  qPrintable( QStringLiteral( "dark pull-to-follow sample was %1" )
                                  .arg( darkPullToFollowBackground.name( QColor::HexArgb ) ) ) );

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
        QTRY_VERIFY_WITH_TIMEOUT( toolBarIconsMatchTheme( false ), 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( iconLightness( searchButton->icon() ) < 80, 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( iconLightness( documentTabs->tabIcon( 0 ) ) < 80, 5000 );
        QCOMPARE( logView->viewport()->size(), darkViewportSize );
        QCOMPARE( logView->horizontalScrollBar()->pageStep(), darkHorizontalPageStep );
        const QImage renderedViewport = logView->viewport()->grab().toImage();
        QVERIFY( !renderedViewport.isNull() );
        QVERIFY( renderedViewport.rect().contains( blankContentPoint ) );
        QVERIFY( renderedViewport.pixelColor( blankContentPoint ).lightness() > 200 );
        QVERIFY( renderedViewport.rect().contains( pullToFollowBackgroundPoint ) );
        const QColor lightPullToFollowBackground
            = renderedViewport.pixelColor( pullToFollowBackgroundPoint );
        const QColor lightGaugeWindowStop = gaugeWindowStop();
        QVERIFY2(
            lightPullToFollowBackground.lightness() > 200
                && lightGaugeWindowStop.isValid() && lightGaugeWindowStop.lightness() > 200,
            qPrintable( QStringLiteral( "light samples remained stale: pull-to-follow=%1, "
                                        "active-gauge-window-stop=%2" )
                            .arg( lightPullToFollowBackground.name( QColor::HexArgb ),
                                  lightGaugeWindowStop.name( QColor::HexArgb ) ) ) );
        QCOMPARE( lightPullToFollowBackground,
                  logView->palette().color( logView->backgroundRole() ) );
        QCOMPARE( lightGaugeWindowStop, crawler->palette().color( QPalette::Window ) );
        QVERIFY( searchInfoLine->palette().brush( searchInfoLine->backgroundRole() ).gradient()
                 != nullptr );

        QVERIFY( QMetaObject::invokeMethod( crawler, "stopSearch", Qt::DirectConnection ) );
        QVERIFY( searchInfoLine->palette().brush( searchInfoLine->backgroundRole() ).gradient()
                 == nullptr );
        Q_EMIT first->uiThemeChanged( UiThemeMode::Dark );
        QTRY_COMPARE_WITH_TIMEOUT( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark,
                                   5000 );
        QTRY_VERIFY_WITH_TIMEOUT( toolBarIconsMatchTheme( true ), 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( iconLightness( searchButton->icon() ) > 200, 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( iconLightness( documentTabs->tabIcon( 0 ) ) > 200, 5000 );
        QTRY_VERIFY_WITH_TIMEOUT( crawler->palette().color( QPalette::Window ).lightness() < 100,
                                  5000 );
        QTRY_VERIFY_WITH_TIMEOUT(
            searchInfoLine->palette().brush( searchInfoLine->backgroundRole() ).gradient()
                == nullptr,
            5000 );
        QCOMPARE( searchInfoLine->palette().color( QPalette::Window ),
                  crawler->palette().color( QPalette::Window ) );

        Q_EMIT first->uiThemeChanged( UiThemeMode::Light );
        QTRY_COMPARE_WITH_TIMEOUT( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Light,
                                   5000 );
        QTRY_VERIFY_WITH_TIMEOUT( crawler->palette().color( QPalette::Window ).lightness() > 200,
                                  5000 );
        searchInfoLine->displayGauge( 37 );
        auto* const searchEdit
            = crawler->findChild<QComboBox*>( QStringLiteral( "mainSearchEdit" ) );
        QVERIFY( searchEdit );
        QToolButton* regexpButton = nullptr;
        for ( auto* const button : crawler->findChildren<QToolButton*>() ) {
            if ( button->toolTip() == QStringLiteral( "Use regex" ) ) {
                regexpButton = button;
                break;
            }
        }
        QVERIFY( regexpButton );
        regexpButton->setChecked( true );
        searchEdit->setEditText( QStringLiteral( "[" ) );
        QVERIFY( QMetaObject::invokeMethod( crawler, "startNewSearch", Qt::DirectConnection ) );
        QTRY_VERIFY_WITH_TIMEOUT(
            searchInfoLine->text().startsWith( QStringLiteral( "Error in expression" ) ), 5000 );
        QVERIFY( searchInfoLine->palette().brush( searchInfoLine->backgroundRole() ).gradient()
                 == nullptr );
        const QColor errorWindowColor = searchInfoLine->palette().color( QPalette::Window );

        Q_EMIT first->uiThemeChanged( UiThemeMode::Dark );
        QTRY_COMPARE_WITH_TIMEOUT( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark,
                                   5000 );
        QTRY_VERIFY_WITH_TIMEOUT(
            searchInfoLine->palette().brush( searchInfoLine->backgroundRole() ).gradient()
                == nullptr,
            5000 );
        QCOMPARE( searchInfoLine->palette().color( QPalette::Window ), errorWindowColor );
        logView->followSet( false );
        Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::Dark );
        QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

        Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::HighContrast );
        QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

        Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::System );
        QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
        QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

        Q_EMIT first->uiThemeChanged( UiThemeMode::System );
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
