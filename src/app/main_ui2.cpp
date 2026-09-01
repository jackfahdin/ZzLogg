#include <utility>

#include <ZzCore/ZzError.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include "applicationrunner.h"
#include "zzlogguiruntime.h"

int main( int argc, char* argv[] )
{
    KloggApplicationOptions options;
    const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if ( !bootstrap ) {
        options.startupWarning = QStringLiteral( "Fluent window initialization failed: %1" )
                                     .arg( bootstrap.error().technicalMessage() );
    }
    else {
        options.createUiRuntime
            = []( KloggApp& app, QString* error ) { return ZzLoggUiRuntime::create( app, error ); };
    }
    return runKloggApplication( argc, argv, std::move( options ) );
}
