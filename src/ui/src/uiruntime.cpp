#include "uiruntime.h"

#include <exception>
#include <optional>

#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>
#include <QVariant>

#include <ZzCore/ZzError.h>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeMode.h>

#include "kloggapp.h"
#include "log.h"
#include "mainwindow.h"
#include "windowchrome.h"

namespace {

ZzFluentUI::ZzThemeMode toZzThemeMode( UiThemeMode mode )
{
    switch ( mode ) {
    case UiThemeMode::Light:
        return ZzFluentUI::ZzThemeMode::Light;
    case UiThemeMode::Dark:
        return ZzFluentUI::ZzThemeMode::Dark;
    case UiThemeMode::System:
    default:
        return ZzFluentUI::ZzThemeMode::System;
    }
}

std::optional<UiThemeMode> fromZzThemeMode( ZzFluentUI::ZzThemeMode mode )
{
    switch ( mode ) {
    case ZzFluentUI::ZzThemeMode::Light:
        return UiThemeMode::Light;
    case ZzFluentUI::ZzThemeMode::Dark:
        return UiThemeMode::Dark;
    case ZzFluentUI::ZzThemeMode::HighContrast:
    case ZzFluentUI::ZzThemeMode::System:
    default:
        return std::nullopt;
    }
}

UiThemeMode resolveExplicitThemeMode( UiThemeMode configuredMode,
                                      ZzFluentUI::ZzThemeController& theme )
{
    if ( configuredMode != UiThemeMode::System ) {
        return configuredMode;
    }

    theme.setMode( ZzFluentUI::ZzThemeMode::System );
    return theme.resolvedMode() == ZzFluentUI::ZzThemeMode::Dark
        ? UiThemeMode::Dark
        : UiThemeMode::Light;
}

} // namespace

UiRuntime::UiRuntime( KloggApp& app )
    : app_( &app )
{
}

std::unique_ptr<UiRuntime> UiRuntime::create( KloggApp& app, QString* error )
{
    if ( error != nullptr ) {
        error->clear();
    }

    try {
        auto runtime = std::unique_ptr<UiRuntime>( new UiRuntime( app ) );
        app.setProperty( "zzlogg.fluentUi", true );
        runtime->theme_ = std::make_unique<ZzFluentUI::ZzThemeController>();
        auto& configuration = Configuration::get();
        const UiThemeMode configuredMode = configuration.uiThemeMode();
        const UiThemeMode explicitMode
            = resolveExplicitThemeMode( configuredMode, *runtime->theme_ );
        if ( configuredMode == UiThemeMode::System ) {
            configuration.setUiThemeMode( explicitMode );
            configuration.save();
        }
        runtime->theme_->setMode( toZzThemeMode( explicitMode ) );
        app.setStyle( new ZzFluentUI::ZzFluentStyle( runtime->theme_.get() ) );
        app.setMainWindowFactory( [ runtimePtr = runtime.get() ](WindowSession session) {
            return runtimePtr->createWindow(std::move(session));
        } );
        return runtime;
    } catch ( const std::exception& exception ) {
        if ( error != nullptr ) {
            *error = QString::fromLocal8Bit( exception.what() );
        }
    } catch ( ... ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "Unknown Fluent UI initialization failure" );
        }
    }

    app.setMainWindowFactory( {} );
    app.setProperty( "zzlogg.fluentUi", QVariant{} );
    return {};
}

UiRuntime::~UiRuntime()
{
    if ( app_ != nullptr ) {
        app_->setMainWindowFactory( {} );
        app_->destroyMainWindows();
        app_->setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );
    }
    theme_.reset();
}

MainWindow* UiRuntime::createWindow(WindowSession session)
{
    auto window = std::make_unique<MainWindow>(std::move(session), UiThemeContext{*theme_});
    connect(window.get(), &MainWindow::uiThemeChanged, this,
            [this](UiThemeMode mode) { applyTheme(mode, false); });
    connect(window->windowChrome(), &WindowChrome::themeModeRequested, this,
            [this](ZzFluentUI::ZzThemeMode mode) {
                if (const auto requested = fromZzThemeMode(mode)) applyTheme(*requested, true);
            });
    return window.release();
}

void UiRuntime::applyTheme( UiThemeMode mode, bool persist )
{
    if ( mode == UiThemeMode::System ) {
        return;
    }

    if ( persist ) {
        auto& configuration = Configuration::get();
        configuration.setUiThemeMode( mode );
        configuration.save();
    }
    theme_->setMode( toZzThemeMode( mode ) );
}
