#include "windowchrome.h"
#include "log.h"

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
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzFluentUI/ZzTitleBarMenuDisplayMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>
#include <ZzWindowKit/ZzWindowCapability.h>

namespace {
ZzWindowKit::ZzWindowChromeConfiguration
buildFluentChromeConfiguration( ZzFluentUI::ZzFluentTitleBar& titleBar,
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
        = buildFluentChromeConfiguration( titleBar, capabilities );
    auto configured = agent.configureChrome( chrome );
    if ( configured ) {
        LOG_DEBUG << "Fluent window capabilities: " << capabilities.toInt();
    }
    return configured;
}

} // namespace

QString formatWindowTitle( const QString& documentName )
{
    return documentName.isEmpty() ? QStringLiteral( "ZzLogg" )
                                  : QStringLiteral( "%1 \u2014 ZzLogg" ).arg( documentName );
}

WindowChrome::WindowChrome(QMainWindow& window, UiThemeContext context,
                           ChromeConfigurator configure)
    : window_(&window), theme_(&context.controller)
{
    const auto flags = window.windowFlags();
    try {
        auto title = std::make_unique<ZzFluentUI::ZzFluentTitleBar>(&window);
        title->setObjectName(QStringLiteral("zzloggFluentTitleBar"));
        title->setMenuDisplayMode(ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive);
        title->setThemeInteractionMode(ZzFluentUI::ZzTitleBarThemeInteractionMode::Toggle);
        for (auto* action : title->themeMenu()->actions()) {
            const auto mode = static_cast<ZzFluentUI::ZzThemeMode>(action->data().toInt());
            action->setVisible(mode == ZzFluentUI::ZzThemeMode::Light
                               || mode == ZzFluentUI::ZzThemeMode::Dark);
        }
        auto agent = std::make_unique<ZzWindowKit::ZzWindowAgent>();
        const auto result = (configure ? configure : configureDefaultChrome)(window, *title, *agent);
        if (!result) {
            LOG_WARNING << "Window chrome configuration failed: "
                        << result.error().technicalMessage();
            agent.reset();
            title.reset();
            window.setWindowFlags(flags);
            menu_ = window.menuBar();
            return;
        }
        titleBar_ = title.get();
        menu_ = title->menuBar();
        agent_ = std::move(agent);
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::minimizeRequested,
                this, [&window] { window.showMinimized(); });
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::maximizeRestoreRequested,
                this, [&window] {
                    window.isMaximized() ? window.showNormal() : window.showMaximized();
                });
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::closeRequested,
                this, [&window] { window.close(); });
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::alwaysOnTopRequested,
                this, &WindowChrome::setAlwaysOnTop);
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::themeModeRequested,
                this, &WindowChrome::themeModeRequested);
        connect(title.get(), &ZzFluentUI::ZzFluentTitleBar::themeToggleRequested,
                this, &WindowChrome::requestThemeToggle);
        connect(&context.controller, &ZzFluentUI::ZzThemeController::snapshotChanged,
                this, [this] { syncTheme(); });
        title->setAlwaysOnTop(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        window.setMenuWidget(title.release());
        window.installEventFilter(this);
        setDocumentName({});
        syncWindowState();
        syncTheme();
    } catch (const std::exception& error) {
        LOG_WARNING << "Window chrome creation failed: " << error.what();
        agent_.reset();
        titleBar_.clear();
        window.setWindowFlags(flags);
        menu_ = window.menuBar();
    } catch (...) {
        LOG_WARNING << "Window chrome creation failed with an unknown exception";
        agent_.reset();
        titleBar_.clear();
        window.setWindowFlags(flags);
        menu_ = window.menuBar();
    }
}

QMenuBar& WindowChrome::commandMenuBar() const { return *menu_; }
bool WindowChrome::usesNativeFallback() const { return titleBar_.isNull(); }

WindowChrome::~WindowChrome()
{
    if ( !window_.isNull() ) {
        window_->removeEventFilter( this );
    }
}

void WindowChrome::setDocumentName( const QString& documentName )
{
    if ( window_.isNull() || titleBar_.isNull() ) {
        return;
    }
    const QString title = formatWindowTitle( documentName );
    window_->setWindowTitle( title );
    titleBar_->setTitle( title );
}

bool WindowChrome::eventFilter( QObject* watched, QEvent* event )
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

void WindowChrome::syncWindowState()
{
    if ( window_.isNull() || titleBar_.isNull() ) {
        return;
    }
    titleBar_->setMaximized( window_->isMaximized() );
    titleBar_->setWindowIcon( window_->windowIcon() );
}

void WindowChrome::syncTheme()
{
    if ( !titleBar_.isNull() && !theme_.isNull() ) {
        titleBar_->setThemeMode( theme_->mode() );
    }
}

void WindowChrome::requestThemeToggle()
{
    if ( theme_.isNull() ) {
        return;
    }
    const auto snapshot = theme_->snapshot();
    if ( !snapshot ) {
        return;
    }
    Q_EMIT themeModeRequested( snapshot->mode() == ZzFluentUI::ZzThemeMode::Dark
                                   ? ZzFluentUI::ZzThemeMode::Light
                                   : ZzFluentUI::ZzThemeMode::Dark );
}

void WindowChrome::setAlwaysOnTop( bool requested )
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
