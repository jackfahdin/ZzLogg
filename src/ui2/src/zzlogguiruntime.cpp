#include "zzlogguiruntime.h"

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
#include "zzloggfluentshell.h"

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

ZzLoggUiRuntime::ZzLoggUiRuntime( KloggApp& app )
    : app_( &app )
{
}

std::unique_ptr<ZzLoggUiRuntime> ZzLoggUiRuntime::create( KloggApp& app, QString* error )
{
    if ( error != nullptr ) {
        error->clear();
    }

    try {
        auto runtime = std::unique_ptr<ZzLoggUiRuntime>( new ZzLoggUiRuntime( app ) );
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
        app.setWindowDecorator( [ runtimePtr = runtime.get() ]( MainWindow& window ) {
            runtimePtr->decorate( window );
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

    app.setWindowDecorator( {} );
    app.setProperty( "zzlogg.fluentUi", QVariant{} );
    return {};
}

ZzLoggUiRuntime::~ZzLoggUiRuntime()
{
    if ( app_ != nullptr ) {
        app_->setWindowDecorator( {} );
        app_->setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );
    }
    theme_.reset();
}

void ZzLoggUiRuntime::decorate( MainWindow& window )
{
    try {
        const auto installed = ZzLoggFluentShell::install( window, *theme_ );
        if ( !installed ) {
            const ZzCore::ZzError& error = installed.error();
            LOG_WARNING << "Fluent shell installation failed: code="
                        << static_cast<int>( error.code() )
                        << ", technical=" << error.technicalMessage()
                        << ", context=" << error.context();
            const QString message = error.technicalMessage();
            QTimer::singleShot( 0, &window, [ &window, message ] {
                QMessageBox::warning( &window, QObject::tr( "ZzLogg UI" ), message );
            } );
            return;
        }

        ZzLoggFluentShell* const shell = installed.value();
        connect( &window, &MainWindow::activeDocumentNameChanged, shell,
                 &ZzLoggFluentShell::setActiveDocumentName );
        connect( &window, &MainWindow::uiThemeChanged, this,
                 [ this ]( UiThemeMode mode ) { applyTheme( mode, false ); } );
        connect( shell, &ZzLoggFluentShell::themeModeRequested, this,
                 [ this ]( ZzFluentUI::ZzThemeMode mode ) {
                     if ( const auto requestedMode = fromZzThemeMode( mode ) ) {
                         applyTheme( *requestedMode, true );
                     }
                 } );
    } catch ( const std::exception& exception ) {
        LOG_WARNING << "Fluent window decoration threw: " << exception.what();
    } catch ( ... ) {
        LOG_WARNING << "Fluent window decoration threw an unknown exception";
    }
}

void ZzLoggUiRuntime::applyTheme( UiThemeMode mode, bool persist )
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
