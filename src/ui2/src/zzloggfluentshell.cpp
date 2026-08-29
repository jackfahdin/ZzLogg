#include "zzloggfluentshell.h"

#include <exception>
#include <utility>

#include <QAction>
#include <QEvent>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzTitleBarMenuDisplayMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>
#include <ZzWindowKit/ZzWindowCapability.h>
#include <ZzWindowKit/ZzWindowChromeConfiguration.h>

namespace {

ZzCore::ZzResult<ZzLoggFluentShell*> shellFailure( ZzCore::ZzError error )
{
    return ZzCore::ZzResult<ZzLoggFluentShell*>::failure( std::move( error ) );
}

ZzCore::ZzResult<void> configureDefaultChrome( QMainWindow& window,
                                               ZzFluentUI::ZzFluentTitleBar& titleBar,
                                               ZzWindowKit::ZzWindowAgent& agent )
{
    auto attached = agent.attach( &window );
    if ( !attached ) {
        return attached;
    }

    const bool nativeButtons
        = agent.capabilities().testFlag( ZzWindowKit::ZzWindowCapability::NativeSystemButtons );
    titleBar.setSystemButtonsVisible( !nativeButtons );
    ZzWindowKit::ZzWindowChromeConfiguration chrome;
    chrome.titleBar = &titleBar;
    chrome.windowIcon = titleBar.windowIconWidget();
    chrome.interactiveWidgets = titleBar.hitTestVisibleWidgets();
    if ( !nativeButtons ) {
        chrome.minimizeButton = titleBar.minimizeButton();
        chrome.maximizeButton = titleBar.maximizeButton();
        chrome.closeButton = titleBar.closeButton();
    }
    return agent.configureChrome( chrome );
}

} // namespace

QString formatZzLoggWindowTitle( const QString& documentName )
{
    return documentName.isEmpty() ? QStringLiteral( "ZzLogg" )
                                  : QStringLiteral( "%1 \u2014 ZzLogg" ).arg( documentName );
}

ZzCore::ZzResult<ZzLoggFluentShell*>
ZzLoggFluentShell::install( QMainWindow& window, ZzFluentUI::ZzThemeController& theme,
                            ChromeConfigurator chromeConfigurator )
{
    const Qt::WindowFlags originalFlags = window.windowFlags();
    QMenuBar* const originalMenuBar = window.menuBar();
    const QList<QAction*> originalActions = originalMenuBar->actions();
    auto titleBar = std::make_unique<ZzFluentUI::ZzFluentTitleBar>( &window );
    titleBar->setObjectName( QStringLiteral( "zzloggFluentTitleBar" ) );
    titleBar->setMenuDisplayMode( ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive );
    auto agent = std::make_unique<ZzWindowKit::ZzWindowAgent>();

    if ( !chromeConfigurator ) {
        chromeConfigurator = configureDefaultChrome;
    }
    ZzCore::ZzResult<void> configured = [ & ] {
        try {
            return chromeConfigurator( window, *titleBar, *agent );
        } catch ( const std::exception& exception ) {
            return ZzCore::ZzResult<void>::failure( ZzCore::ZzError(
                ZzCore::ZzErrorCode::Unknown, QStringLiteral( "Fluent chrome configurator threw" ),
                QString::fromLocal8Bit( exception.what() ) ) );
        } catch ( ... ) {
            return ZzCore::ZzResult<void>::failure(
                ZzCore::ZzError( ZzCore::ZzErrorCode::Unknown,
                                 QStringLiteral( "Fluent chrome configurator threw" ) ) );
        }
    }();
    if ( !configured ) {
        const ZzCore::ZzError error = configured.error();
        agent.reset();
        titleBar.reset();
        window.setWindowFlags( originalFlags );
        return shellFailure( error );
    }

    QMenuBar* const fluentMenuBar = titleBar->menuBar();
    for ( QAction* action : originalActions ) {
        if ( QMenu* menu = action->menu() ) {
            menu->setParent( fluentMenuBar );
        }
        else {
            action->setParent( fluentMenuBar );
        }
        fluentMenuBar->addAction( action );
    }
    originalMenuBar->clear();
    originalMenuBar->hide();
    ZzFluentUI::ZzFluentTitleBar* const retainedTitleBar = titleBar.release();
    window.setMenuWidget( retainedTitleBar );

    auto* shell = new ZzLoggFluentShell( window, originalMenuBar, retainedTitleBar,
                                         std::move( agent ), theme );
    window.setProperty( "zzlogg.fluentShellInstalled", true );
    window.installEventFilter( shell );
    QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::minimizeRequested, shell,
                      [ &window ] { window.showMinimized(); } );
    QObject::connect(
        retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::maximizeRestoreRequested, shell,
        [ &window ] { window.isMaximized() ? window.showNormal() : window.showMaximized(); } );
    QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::closeRequested, shell,
                      [ &window ] { window.close(); } );
    QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::alwaysOnTopRequested, shell,
                      [ shell ]( bool requested ) { shell->setAlwaysOnTop( requested ); } );
    QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::themeModeRequested, shell,
                      &ZzLoggFluentShell::themeModeRequested );
    QObject::connect( &theme, &ZzFluentUI::ZzThemeController::snapshotChanged, shell,
                      [ shell ] { shell->syncTheme(); } );
    shell->setActiveDocumentName( {} );
    shell->syncWindowState();
    shell->syncTheme();
    retainedTitleBar->setAlwaysOnTop( window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
    return ZzCore::ZzResult<ZzLoggFluentShell*>::success( shell );
}

ZzLoggFluentShell::ZzLoggFluentShell( QMainWindow& window, QMenuBar* originalMenuBar,
                                      ZzFluentUI::ZzFluentTitleBar* titleBar,
                                      std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent,
                                      ZzFluentUI::ZzThemeController& theme )
    : QObject( &window )
    , window_( &window )
    , originalMenuBar_( originalMenuBar )
    , titleBar_( titleBar )
    , agent_( std::move( agent ) )
    , theme_( &theme )
{
}

ZzLoggFluentShell::~ZzLoggFluentShell()
{
    if ( !window_.isNull() ) {
        window_->removeEventFilter( this );
    }
}

void ZzLoggFluentShell::setActiveDocumentName( const QString& documentName )
{
    if ( window_.isNull() || titleBar_.isNull() ) {
        return;
    }
    const QString title = formatZzLoggWindowTitle( documentName );
    window_->setWindowTitle( title );
    titleBar_->setTitle( title );
}

bool ZzLoggFluentShell::eventFilter( QObject* watched, QEvent* event )
{
    if ( !window_.isNull() && watched == window_ && event != nullptr ) {
        if ( event->type() == QEvent::WindowStateChange ) {
            syncWindowState();
        }
        else if ( event->type() == QEvent::WindowIconChange && !titleBar_.isNull() ) {
            titleBar_->setWindowIcon( window_->windowIcon() );
        }
    }
    return QObject::eventFilter( watched, event );
}

void ZzLoggFluentShell::syncWindowState()
{
    if ( window_.isNull() || titleBar_.isNull() ) {
        return;
    }
    titleBar_->setMaximized( window_->isMaximized() );
    titleBar_->setWindowIcon( window_->windowIcon() );
}

void ZzLoggFluentShell::syncTheme()
{
    if ( !titleBar_.isNull() && !theme_.isNull() ) {
        titleBar_->setThemeMode( theme_->mode() );
    }
}

void ZzLoggFluentShell::setAlwaysOnTop( bool requested )
{
    if ( window_.isNull() || titleBar_.isNull() ) {
        return;
    }
    const bool wasVisible = window_->isVisible();
    const auto previousState = window_->windowState();
    window_->setWindowFlag( Qt::WindowStaysOnTopHint, requested );
    window_->setWindowState( previousState );
    wasVisible ? window_->show() : window_->hide();
    titleBar_->setAlwaysOnTop( window_->windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
}
