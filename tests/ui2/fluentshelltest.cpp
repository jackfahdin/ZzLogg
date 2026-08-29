#include <QtTest>

#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSplitter>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzColorToken.h>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzFluentUI/ZzTitleBarMenuDisplayMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>

#include "zzloggfluentshell.h"

namespace {

class CloseProbeWindow final : public QMainWindow {
public:
    int closeEventCount = 0;

protected:
    void closeEvent( QCloseEvent* event ) override
    {
        ++closeEventCount;
        event->ignore();
    }
};

class ApplicationStyleReset final {
public:
    ~ApplicationStyleReset()
    {
        QApplication::setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );
    }
};

ZzFluentUI::ZzFluentTitleBar* titleBarFor( QMainWindow& window )
{
    return window.findChild<ZzFluentUI::ZzFluentTitleBar*>(
        QStringLiteral( "zzloggFluentTitleBar" ) );
}

} // namespace

void verifyTitleFormatting()
{
    QCOMPARE( formatZzLoggWindowTitle( {} ), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( formatZzLoggWindowTitle( QStringLiteral( "server.log" ) ),
              QStringLiteral( "server.log \u2014 ZzLogg" ) );
    QCOMPARE( formatZzLoggWindowTitle( QStringLiteral( "\u670d\u52a1\u5668-\U0001f680.log" ) ),
              QStringLiteral( "\u670d\u52a1\u5668-\U0001f680.log \u2014 ZzLogg" ) );

    const QString longName( 512, QLatin1Char( 'x' ) );
    const QString formatted = formatZzLoggWindowTitle( longName );
    QCOMPARE( formatted, longName + QStringLiteral( " \u2014 ZzLogg" ) );
    QCOMPARE( formatted.size(), 521 );
}

void verifySuccessfulMenuMigration()
{
    QMainWindow window;
    auto* central = new QSplitter( &window );
    central->setObjectName( QStringLiteral( "fixtureSplitter" ) );
    central->addWidget( new QWidget( central ) );
    central->addWidget( new QWidget( central ) );
    window.setCentralWidget( central );

    auto* toolbar = new QToolBar( QStringLiteral( "Fixture" ), &window );
    toolbar->setObjectName( QStringLiteral( "fixtureToolbar" ) );
    window.addToolBar( toolbar );

    QMenuBar* const originalMenuBar = window.menuBar();
    auto* fileMenu = originalMenuBar->addMenu( QStringLiteral( "&File" ) );
    auto* editMenu = originalMenuBar->addMenu( QStringLiteral( "&Edit" ) );
    QAction* const openAction = fileMenu->addAction( QStringLiteral( "Open" ) );
    openAction->setShortcut( QKeySequence::Open );
    openAction->setCheckable( true );
    openAction->setChecked( true );
    QAction* const disabledAction = editMenu->addAction( QStringLiteral( "Disabled" ) );
    disabledAction->setEnabled( false );
    QAction* const topSeparator = originalMenuBar->addSeparator();
    auto* topAction = new QAction( QStringLiteral( "Help" ), originalMenuBar );
    originalMenuBar->addAction( topAction );

    const QList<QAction*> originalTopLevelActions = originalMenuBar->actions();
    QWidget* const originalCentral = window.centralWidget();
    QToolBar* const originalToolbar = toolbar;
    const QPointer<QMenu> fileGuard( fileMenu );
    const QPointer<QAction> openGuard( openAction );
    const QPointer<QAction> separatorGuard( topSeparator );
    const QPointer<QAction> topActionGuard( topAction );

    ZzFluentUI::ZzThemeController theme;
    const auto result = ZzLoggFluentShell::install( window, theme );

    QVERIFY( result );
    QVERIFY( window.property( "zzlogg.fluentShellInstalled" ).toBool() );
    QCOMPARE( window.centralWidget(), originalCentral );
    QCOMPARE( window.findChild<QToolBar*>( QStringLiteral( "fixtureToolbar" ) ), originalToolbar );
    auto* titleBar = window.findChild<ZzFluentUI::ZzFluentTitleBar*>(
        QStringLiteral( "zzloggFluentTitleBar" ) );
    QVERIFY( titleBar );
    QCOMPARE( window.menuWidget(), static_cast<QWidget*>( titleBar ) );
    QVERIFY( originalMenuBar->isHidden() );
    QCOMPARE( titleBar->menuDisplayMode(), ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive );
    QCOMPARE( titleBar->menuBar()->actions(), originalTopLevelActions );
    QCOMPARE( titleBar->menuBar()->actions().at( 0 )->menu(), fileMenu );

    QVERIFY( !fileGuard.isNull() );
    QVERIFY( !openGuard.isNull() );
    QVERIFY( !separatorGuard.isNull() );
    QVERIFY( !topActionGuard.isNull() );
    QCOMPARE( fileMenu->parent(), static_cast<QObject*>( titleBar->menuBar() ) );
    QCOMPARE( topSeparator->parent(), static_cast<QObject*>( titleBar->menuBar() ) );
    QCOMPARE( topAction->parent(), static_cast<QObject*>( titleBar->menuBar() ) );
    QCOMPARE( openAction->shortcut(), QKeySequence::Open );
    QVERIFY( openAction->isChecked() );
    QVERIFY( !disabledAction->isEnabled() );
    QVERIFY( topSeparator->isSeparator() );
    bool topActionTriggered = false;
    QObject::connect( topAction, &QAction::triggered, &window,
                      [ &topActionTriggered ] { topActionTriggered = true; } );
    topAction->trigger();
    QVERIFY( topActionTriggered );
}

void verifyConfigureFailureRollsBack()
{
    QMainWindow window;
    QMenuBar* const originalMenuBar = window.menuBar();
    QMenu* const fileMenu = originalMenuBar->addMenu( QStringLiteral( "File" ) );
    QAction* const triggerable = fileMenu->addAction( QStringLiteral( "Trigger" ) );
    QAction* const secondTriggerable = fileMenu->addAction( QStringLiteral( "Second" ) );
    const QList<QAction*> originalFileActions = fileMenu->actions();
    int triggered = 0;
    QObject::connect( triggerable, &QAction::triggered, &window, [ &triggered ] { ++triggered; } );
    QObject::connect( secondTriggerable, &QAction::triggered, &window,
                      [ &triggered ] { ++triggered; } );
    const Qt::WindowFlags originalFlags = window.windowFlags();

    const auto failAfterAttach = []( QMainWindow& host, ZzFluentUI::ZzFluentTitleBar&,
                                     ZzWindowKit::ZzWindowAgent& agent ) {
        auto attached = agent.attach( &host );
        if ( !attached )
            return attached;
        return ZzCore::ZzResult<void>::failure( ZzCore::ZzError(
            ZzCore::ZzErrorCode::Backend, QStringLiteral( "injected configure failure" ) ) );
    };

    ZzFluentUI::ZzThemeController theme;
    const auto result = ZzLoggFluentShell::install( window, theme, failAfterAttach );

    QVERIFY( !result );
    QCOMPARE( window.windowFlags(), originalFlags );
    QCOMPARE( window.menuBar(), originalMenuBar );
    QCOMPARE( originalMenuBar->actions(), QList<QAction*>( { fileMenu->menuAction() } ) );
    QVERIFY( !originalMenuBar->isHidden() );
    QVERIFY( !window.property( "zzlogg.fluentShellInstalled" ).toBool() );
    QVERIFY(
        window.findChild<ZzFluentUI::ZzFluentTitleBar*>( QStringLiteral( "zzloggFluentTitleBar" ) )
        == nullptr );
    QCOMPARE( fileMenu->actions(), originalFileActions );
    triggerable->trigger();
    secondTriggerable->trigger();
    QCOMPARE( triggered, 2 );
}

void verifyActiveDocumentTitleSynchronization()
{
    QMainWindow window;
    ZzFluentUI::ZzThemeController theme;
    const auto installed = ZzLoggFluentShell::install( window, theme );
    QVERIFY( installed );
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    QCOMPARE( window.windowTitle(), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( titleBar->title(), QStringLiteral( "ZzLogg" ) );

    installed.value()->setActiveDocumentName( QStringLiteral( "\u65e5\u5fd7-\U0001f680.log" ) );
    QCOMPARE( window.windowTitle(), QStringLiteral( "\u65e5\u5fd7-\U0001f680.log \u2014 ZzLogg" ) );
    QCOMPARE( titleBar->title(), window.windowTitle() );

    const QString longName( 512, QLatin1Char( 'L' ) );
    installed.value()->setActiveDocumentName( longName );
    QCOMPARE( window.windowTitle(), longName + QStringLiteral( " \u2014 ZzLogg" ) );
    QCOMPARE( titleBar->title(), window.windowTitle() );
    QCOMPARE( titleBar->title().size(), 521 );
}

void verifyChromeStateAndIconSynchronization()
{
    QMainWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    const auto installed = ZzLoggFluentShell::install( window, theme );
    QVERIFY( installed );
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    const QList<QWidget*> hitTestWidgets = titleBar->hitTestVisibleWidgets();
    QVERIFY( hitTestWidgets.contains( titleBar->menuBar() ) );
    QVERIFY( !hitTestWidgets.contains( titleBar ) );
    for ( QWidget* widget : hitTestWidgets ) {
        QVERIFY( widget );
        QVERIFY( titleBar->isAncestorOf( widget ) );
    }
#ifdef Q_OS_WIN
    QVERIFY( !titleBar->minimizeButton()->isHidden() );
    QVERIFY( !titleBar->maximizeButton()->isHidden() );
    QVERIFY( !titleBar->closeButton()->isHidden() );
#endif

    QPixmap iconPixmap( 16, 16 );
    iconPixmap.fill( QColor( 220, 30, 40 ) );
    window.setWindowIcon( QIcon( iconPixmap ) );
    QCoreApplication::processEvents();
    auto* iconLabel = qobject_cast<QLabel*>( titleBar->windowIconWidget() );
    QVERIFY( iconLabel );
    const QImage titleIcon = iconLabel->pixmap().toImage();
    QVERIFY( !titleIcon.isNull() );
    QCOMPARE( titleIcon.pixelColor( titleIcon.width() / 2, titleIcon.height() / 2 ),
              QColor( 220, 30, 40 ) );

    window.showMaximized();
    QTRY_VERIFY( window.isMaximized() );
    QTRY_COMPARE( titleBar->maximizeButton()->accessibleName(), QStringLiteral( "\u8fd8\u539f" ) );
    window.showNormal();
    QTRY_VERIFY( !window.isMaximized() );
    QTRY_COMPARE( titleBar->maximizeButton()->accessibleName(),
                  QStringLiteral( "\u6700\u5927\u5316" ) );
    window.close();
}

void verifyWindowButtonIntents()
{
    CloseProbeWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    const auto installed = ZzLoggFluentShell::install( window, theme );
    QVERIFY( installed );
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );
    window.show();
    QTRY_VERIFY( window.isVisible() );

    QVERIFY( QMetaObject::invokeMethod( titleBar, "minimizeRequested" ) );
    QTRY_VERIFY( window.isMinimized() );
    window.showNormal();
    QTRY_VERIFY( !window.isMinimized() );

    QVERIFY( QMetaObject::invokeMethod( titleBar, "maximizeRestoreRequested" ) );
    QTRY_VERIFY( window.isMaximized() );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "maximizeRestoreRequested" ) );
    QTRY_VERIFY( !window.isMaximized() );

    QCOMPARE( window.closeEventCount, 0 );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "closeRequested" ) );
    QCOMPARE( window.closeEventCount, 1 );
    window.hide();
}

void verifyAlwaysOnTopPreservesWindowPresentation()
{
    QMainWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    const auto installed = ZzLoggFluentShell::install( window, theme );
    QVERIFY( installed );
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    window.setWindowState( Qt::WindowMinimized );
    const auto hiddenState = window.windowState();
    QVERIFY( !window.isVisible() );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "alwaysOnTopRequested", Q_ARG( bool, true ) ) );
    QVERIFY( !window.isVisible() );
    QCOMPARE( window.windowState(), hiddenState );
    QVERIFY( window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
    QVERIFY( titleBar->isAlwaysOnTop() );

    window.showMaximized();
    QTRY_VERIFY( window.isVisible() );
    QTRY_VERIFY( window.isMaximized() );
    const auto visibleState = window.windowState();
    QVERIFY( QMetaObject::invokeMethod( titleBar, "alwaysOnTopRequested", Q_ARG( bool, false ) ) );
    QVERIFY( window.isVisible() );
    QCOMPARE( window.windowState(), visibleState );
    QVERIFY( !window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
    QVERIFY( !titleBar->isAlwaysOnTop() );
    window.close();
}

void verifySharedThemeObservationAndForwarding()
{
    ZzFluentUI::ZzThemeController theme;
    theme.setMode( ZzFluentUI::ZzThemeMode::Light );
    QApplication::setStyle( new ZzFluentUI::ZzFluentStyle( &theme ) );
    ApplicationStyleReset styleReset;
    QMainWindow first;
    QMainWindow second;
    QLineEdit ordinaryWidget;
    const auto firstInstalled = ZzLoggFluentShell::install( first, theme );
    const auto secondInstalled = ZzLoggFluentShell::install( second, theme );
    QVERIFY( firstInstalled );
    QVERIFY( secondInstalled );
    auto* firstTitleBar = titleBarFor( first );
    auto* secondTitleBar = titleBarFor( second );
    QVERIFY( firstTitleBar );
    QVERIFY( secondTitleBar );
    QCOMPARE( firstTitleBar->themeMode(), ZzFluentUI::ZzThemeMode::Light );
    QCOMPARE( secondTitleBar->themeMode(), ZzFluentUI::ZzThemeMode::Light );

    QSignalSpy requestSpy( firstInstalled.value(), &ZzLoggFluentShell::themeModeRequested );
    QVERIFY( QMetaObject::invokeMethod(
        firstTitleBar, "themeModeRequested",
        Q_ARG( ZzFluentUI::ZzThemeMode, ZzFluentUI::ZzThemeMode::Dark ) ) );
    QCOMPARE( requestSpy.count(), 1 );
    QCOMPARE( requestSpy.at( 0 ).at( 0 ).value<ZzFluentUI::ZzThemeMode>(),
              ZzFluentUI::ZzThemeMode::Dark );
    QCOMPARE( theme.mode(), ZzFluentUI::ZzThemeMode::Light );

    const QColor before = ordinaryWidget.palette().color( QPalette::Window );
    theme.setMode( ZzFluentUI::ZzThemeMode::Dark );
    QCOMPARE( firstTitleBar->themeMode(), ZzFluentUI::ZzThemeMode::Dark );
    QCOMPARE( secondTitleBar->themeMode(), ZzFluentUI::ZzThemeMode::Dark );
    const QColor expected = theme.snapshot()->color( ZzFluentUI::ZzColorToken::Surface );
    QCOMPARE( QApplication::palette().color( QPalette::Window ), expected );
    QCoreApplication::processEvents();
    QCOMPARE( ordinaryWidget.palette().color( QPalette::Window ), expected );
    QVERIFY( ordinaryWidget.palette().color( QPalette::Window ) != before );
}

void verifyWindowOwnsShellLifetime()
{
    ZzFluentUI::ZzThemeController theme;
    auto* window = new QMainWindow;
    const auto installed = ZzLoggFluentShell::install( *window, theme );
    QVERIFY( installed );
    QPointer<ZzLoggFluentShell> shellGuard( installed.value() );
    QPointer<ZzFluentUI::ZzFluentTitleBar> titleBarGuard( titleBarFor( *window ) );
    QVERIFY( !shellGuard.isNull() );
    QVERIFY( !titleBarGuard.isNull() );

    delete window;

    QVERIFY( shellGuard.isNull() );
    QVERIFY( titleBarGuard.isNull() );
    theme.setMode( ZzFluentUI::ZzThemeMode::Dark );
}

void verifyConfiguratorExceptionBecomesFailure()
{
    QMainWindow window;
    QMenuBar* const originalMenuBar = window.menuBar();
    originalMenuBar->addMenu( QStringLiteral( "File" ) );
    const Qt::WindowFlags originalFlags = window.windowFlags();
    ZzFluentUI::ZzThemeController theme;
    bool exceptionEscaped = false;
    try {
        const auto result = ZzLoggFluentShell::install(
            window, theme,
            []( QMainWindow& host, ZzFluentUI::ZzFluentTitleBar&,
                ZzWindowKit::ZzWindowAgent& agent ) -> ZzCore::ZzResult<void> {
                auto attached = agent.attach( &host );
                if ( !attached ) {
                    return attached;
                }
                throw std::runtime_error( "injected decorator exception" );
            } );
        QVERIFY( !result );
    } catch ( ... ) {
        exceptionEscaped = true;
    }
    QVERIFY( !exceptionEscaped );
    QCOMPARE( window.windowFlags(), originalFlags );
    QCOMPARE( window.menuBar(), originalMenuBar );
    QVERIFY( !window.property( "zzlogg.fluentShellInstalled" ).toBool() );
    QVERIFY( titleBarFor( window ) == nullptr );
}
