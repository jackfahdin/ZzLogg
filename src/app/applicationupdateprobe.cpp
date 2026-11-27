#include "applicationupdateprobe.h"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdio>
#include <exception>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeMode.h>

#include "storagecontext.h"
#include "zzloggapplicationidentity.h"

int runApplicationUpdateProbe( int argc, char* argv[] )
{
    if ( argc != 2 ) {
        return 64;
    }
    try {
        // Use the real platform plugin (qwindows on Windows) without showing
        // a window. Offscreen alone would miss a broken deployed qwindows.dll.
#ifdef Q_OS_WIN
        qputenv( "QT_QPA_PLATFORM", "windows" );
#endif
        prepareZzLoggApplicationIdentity();
        QTemporaryDir temporary;
        if ( !temporary.isValid() ) {
            return 1;
        }
        QApplication app( argc, argv );
        QString error;
        const StorageLocation location{ StorageMode::CustomDirectory, temporary.path(), {}, true };
        if ( !StorageContext::install( location, &error )
             || !StorageContext::current().ensureDirectories( &error ) ) {
            return 1;
        }
        QSettings settings( StorageContext::current().configFilePath(), QSettings::IniFormat );
        settings.setValue( QStringLiteral( "update/probe" ), true );
        settings.sync();
        if ( settings.status() != QSettings::NoError ) {
            return 1;
        }
        auto* theme = new ZzFluentUI::ZzThemeController( &app );
        theme->setMode( ZzFluentUI::ZzThemeMode::Light );
        app.setStyle( new ZzFluentUI::ZzFluentStyle( theme ) );
        QWidget widget;
        widget.ensurePolished();
        if ( !widget.winId() ) {
            return 1;
        }
        app.processEvents();
        // An exit code alone is insufficient: older binaries or wrappers can
        // exit successfully without implementing this isolated probe.
        constexpr char confirmation[] = "ZZLOGG_UPDATE_PROBE_OK\n";
#ifdef Q_OS_WIN
        // A WIN32 GUI executable need not have a CRT stdout stream. The
        // native updater explicitly supplies this standard handle.
        DWORD written = 0;
        if ( !WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), confirmation,
                         sizeof( confirmation ) - 1, &written, nullptr )
             || written != sizeof( confirmation ) - 1 ) {
            return 1;
        }
#else
        if ( std::fputs( confirmation, stdout ) == EOF || std::fflush( stdout ) != 0 ) {
            return 1;
        }
#endif
        return 0;
    } catch ( const std::exception& error ) {
        std::fprintf( stderr, "Update probe failed: %s\n", error.what() );
        return 1;
    } catch ( ... ) {
        return 1;
    }
}
