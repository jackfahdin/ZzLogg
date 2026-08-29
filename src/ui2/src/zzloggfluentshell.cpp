#include "zzloggfluentshell.h"
#include "zzloggfluentchrome_p.h"
#include "zzloggfluentshell_p.h"

#include <exception>
#include <utility>

#include <QAction>
#include <QEvent>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScopeGuard>
#include <QVariant>

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzTitleBarMenuDisplayMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>
#include <ZzWindowKit/ZzWindowCapability.h>

ZzWindowKit::ZzWindowChromeConfiguration
ZzLoggUi2Internal::buildFluentChromeConfiguration( ZzFluentUI::ZzFluentTitleBar& titleBar,
                                                   ZzWindowKit::ZzWindowCapabilities capabilities )
{
    const bool nativeButtons
        = capabilities.testFlag( ZzWindowKit::ZzWindowCapability::NativeSystemButtons );
    ZzWindowKit::ZzWindowChromeConfiguration chrome;
    chrome.titleBar = &titleBar;
    chrome.windowIcon = titleBar.windowIconWidget();
    chrome.interactiveWidgets = titleBar.hitTestVisibleWidgets();
    if ( !nativeButtons ) {
        chrome.minimizeButton = titleBar.minimizeButton();
        chrome.maximizeButton = titleBar.maximizeButton();
        chrome.closeButton = titleBar.closeButton();
    }
    return chrome;
}

void ZzLoggUi2Internal::commitFluentMenu( QMainWindow& window,
                                          ZzFluentUI::ZzFluentTitleBar& titleBar,
                                          QMenuBar* originalMenuBar,
                                          MenuCommitInterruption interruption )
{
    struct OriginalAction final {
        QAction* action;
        QObject* parent;
    };
    const bool originalMenuHidden = originalMenuBar != nullptr && originalMenuBar->isHidden();
    const QList<QAction*> originalActions
        = originalMenuBar != nullptr ? originalMenuBar->actions() : QList<QAction*>();
    QList<OriginalAction> originalActionOwners;
    originalActionOwners.reserve( originalActions.size() );
    for ( QAction* action : originalActions ) {
        QObject* const parent
            = action->menu() != nullptr ? action->menu()->parent() : action->parent();
        originalActionOwners.append( { action, parent } );
    }
    QMenuBar* const fluentMenuBar = titleBar.menuBar();

    bool committed = false;
    auto rollback = qScopeGuard( [ & ] {
        if ( committed ) {
            return;
        }
        fluentMenuBar->clear();
        if ( originalMenuBar != nullptr ) {
            originalMenuBar->clear();
            for ( const OriginalAction& original : originalActionOwners ) {
                if ( QMenu* menu = original.action->menu() ) {
                    menu->setParent( qobject_cast<QWidget*>( original.parent ) );
                }
                else {
                    original.action->setParent( original.parent );
                }
                originalMenuBar->addAction( original.action );
            }
            originalMenuBar->setHidden( originalMenuHidden );
        }
    } );

    for ( QAction* action : originalActions ) {
        if ( QMenu* menu = action->menu() ) {
            menu->setParent( fluentMenuBar );
        }
        else {
            action->setParent( fluentMenuBar );
        }
        fluentMenuBar->addAction( action );
    }
    if ( originalMenuBar != nullptr ) {
        originalMenuBar->clear();
        originalMenuBar->hide();
    }
    if ( interruption ) {
        interruption();
    }
    window.setMenuWidget( &titleBar );
    committed = true;
}

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

    const ZzWindowKit::ZzWindowCapabilities capabilities = agent.capabilities();
    const bool nativeButtons
        = capabilities.testFlag( ZzWindowKit::ZzWindowCapability::NativeSystemButtons );
    titleBar.setSystemButtonsVisible( !nativeButtons );
    const ZzWindowKit::ZzWindowChromeConfiguration chrome
        = ZzLoggUi2Internal::buildFluentChromeConfiguration( titleBar, capabilities );
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
    try {
        if ( window.property( "zzlogg.fluentShellInstalled" ).toBool()
             || window.findChild<ZzLoggFluentShell*>( QString(), Qt::FindDirectChildrenOnly ) ) {
            return shellFailure( ZzCore::ZzError(
                ZzCore::ZzErrorCode::InvalidState,
                QStringLiteral( "Fluent shell is already installed on this window" ) ) );
        }

        QWidget* const originalMenuWidget = window.menuWidget();
        QMenuBar* const originalMenuBar = qobject_cast<QMenuBar*>( originalMenuWidget );
        if ( originalMenuWidget != nullptr && originalMenuBar == nullptr ) {
            return shellFailure( ZzCore::ZzError(
                ZzCore::ZzErrorCode::InvalidState,
                QStringLiteral( "Fluent shell requires a QMenuBar or no menu widget" ) ) );
        }

        const Qt::WindowFlags originalFlags = window.windowFlags();
        const QString originalTitle = window.windowTitle();
        const QVariant originalInstalledProperty = window.property( "zzlogg.fluentShellInstalled" );
        std::unique_ptr<ZzFluentUI::ZzFluentTitleBar> titleBar;
        std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent;
        std::unique_ptr<ZzLoggFluentShell> shell;
        bool committed = false;
        auto rollback = qScopeGuard( [ & ] {
            if ( committed ) {
                return;
            }
            shell.reset();
            agent.reset();
            titleBar.reset();
            window.setWindowFlags( originalFlags );
            window.setWindowTitle( originalTitle );
            window.setProperty( "zzlogg.fluentShellInstalled", originalInstalledProperty );
        } );

        titleBar = std::make_unique<ZzFluentUI::ZzFluentTitleBar>( &window );
        titleBar->setObjectName( QStringLiteral( "zzloggFluentTitleBar" ) );
        titleBar->setMenuDisplayMode( ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive );
        agent = std::make_unique<ZzWindowKit::ZzWindowAgent>();
        if ( !chromeConfigurator ) {
            chromeConfigurator = configureDefaultChrome;
        }
        const ZzCore::ZzResult<void> configured = chromeConfigurator( window, *titleBar, *agent );
        if ( !configured ) {
            return shellFailure( configured.error() );
        }

        ZzFluentUI::ZzFluentTitleBar* const retainedTitleBar = titleBar.get();
        shell = std::unique_ptr<ZzLoggFluentShell>( new ZzLoggFluentShell(
            window, originalMenuBar, retainedTitleBar, std::move( agent ), theme ) );
        ZzLoggFluentShell* const retainedShell = shell.get();
        retainedShell->setObjectName( QStringLiteral( "zzloggFluentShell" ) );
        window.installEventFilter( retainedShell );
        QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::minimizeRequested,
                          retainedShell, [ &window ] { window.showMinimized(); } );
        QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::maximizeRestoreRequested,
                          retainedShell, [ &window ] {
                              window.isMaximized() ? window.showNormal() : window.showMaximized();
                          } );
        QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::closeRequested,
                          retainedShell, [ &window ] { window.close(); } );
        QObject::connect(
            retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::alwaysOnTopRequested, retainedShell,
            [ retainedShell ]( bool requested ) { retainedShell->setAlwaysOnTop( requested ); } );
        QObject::connect( retainedTitleBar, &ZzFluentUI::ZzFluentTitleBar::themeModeRequested,
                          retainedShell, &ZzLoggFluentShell::themeModeRequested );
        QObject::connect( &theme, &ZzFluentUI::ZzThemeController::snapshotChanged, retainedShell,
                          [ retainedShell ] { retainedShell->syncTheme(); } );
        retainedShell->setActiveDocumentName( {} );
        retainedShell->syncWindowState();
        retainedShell->syncTheme();
        retainedTitleBar->setAlwaysOnTop(
            window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
        window.setProperty( "zzlogg.fluentShellInstalled", true );

        ZzLoggUi2Internal::commitFluentMenu( window, *retainedTitleBar, originalMenuBar );

        titleBar.release();
        shell.release();
        committed = true;
        return ZzCore::ZzResult<ZzLoggFluentShell*>::success( retainedShell );
    } catch ( const std::exception& exception ) {
        return shellFailure( ZzCore::ZzError( ZzCore::ZzErrorCode::Unknown,
                                              QStringLiteral( "Fluent shell installation threw" ),
                                              QString::fromLocal8Bit( exception.what() ) ) );
    } catch ( ... ) {
        return shellFailure( ZzCore::ZzError(
            ZzCore::ZzErrorCode::Unknown, QStringLiteral( "Fluent shell installation threw" ) ) );
    }
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
