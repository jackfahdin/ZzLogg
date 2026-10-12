#include <utility>

#include <QProcess>
#include <QStandardPaths>

#include <cstdlib>
#include <cstdio>

#include <ZzCore/ZzError.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include "applicationrunner.h"
#include "uiruntime.h"
#include "zzlogg/updateqt/updateservice.h"

int main( int argc, char* argv[] )
{
    // A local update feed is a Test trust environment, which the download path
    // only accepts under Qt test mode; enable it before any path resolution.
    if ( zzlogg::updateqt::localUpdateFeedRequested() ) {
        QStandardPaths::setTestModeEnabled( true );
    }
    const QString originalExecutable = QString::fromLocal8Bit( argv[ 0 ] );
    const QString executablePath = resolveRestartExecutablePath( originalExecutable );
    QStringList arguments;
    for ( int index = 1; index < argc; ++index ) {
        arguments.append( QString::fromLocal8Bit( argv[ index ] ) );
    }

    KloggApplicationOptions options;
    const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if ( !bootstrap ) {
        options.startupWarning = QStringLiteral( "Fluent window initialization failed: %1" )
                                     .arg( bootstrap.error().technicalMessage() );
    }
    else {
        options.createUiRuntime
            = []( KloggApp& app, QString* error ) { return UiRuntime::create( app, error ); };
    }
    const int result = runKloggApplication( argc, argv, std::move( options ) );
    if ( result != ZzLoggRestartExitCode ) {
        return result;
    }
    if ( !executablePath.isEmpty() && QProcess::startDetached( executablePath, arguments ) ) {
        return EXIT_SUCCESS;
    }
    const QByteArray diagnostic
        = QStringLiteral( "Failed to restart ZzLogg executable: %1\n" )
              .arg( executablePath.isEmpty() ? originalExecutable : executablePath )
              .toLocal8Bit();
    std::fwrite( diagnostic.constData(), 1, static_cast<size_t>( diagnostic.size() ), stderr );
    std::fflush( stderr );
    return EXIT_FAILURE;
}
