#include "macupdatecontroller_p.h"
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

// This test runs only on macOS against the real Objective-C delegate and Qt.
// It deliberately does not launch Sparkle's installer or terminate the host.
int main( int argc, char** argv )
{
    QApplication app( argc, argv );
    int preparations = 0;
    int cancellations = 0;
    bool saveSucceeds = true;
    auto* delegate = [[ZzLoggSparkleDelegate alloc] init];
    delegate->prepareExit = [&] { ++preparations; return saveSucceeds; };
    delegate->cancelExit = [&] { ++cancellations; };
    auto require = []( bool condition, const char* message ) {
        if ( !condition ) qFatal( "%s", message );
    };

    require( [delegate updaterShouldRelaunchApplication:nil], "Successful save must permit install" );
    require( preparations == 1 && cancellations == 0, "Save runs before installation" );
    require( [delegate updaterShouldRelaunchApplication:nil], "Repeated termination remains prepared" );
    require( preparations == 1, "Repeated termination cannot overwrite saved session" );

    [delegate updater:nil didAbortWithError:[NSError errorWithDomain:@"fixture" code:1 userInfo:nil]];
    require( cancellations == 1, "Installer failure must unfreeze windows" );
    require( [delegate updaterShouldRelaunchApplication:nil], "A failed installation can be retried" );
    require( preparations == 2, "Retry saves the latest session" );
    [delegate updater:nil didAbortWithError:[NSError errorWithDomain:@"fixture" code:2 userInfo:nil]];

    saveSucceeds = false;
    bool warningShown = false;
    QTimer::singleShot( 0, [&] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* warning = qobject_cast<QMessageBox*>( widget ) ) {
                warningShown = true;
                warning->accept();
            }
        }
    } );
    require( ![delegate updaterShouldRelaunchApplication:nil], "Failed save must veto installation" );
    require( preparations == 3 && cancellations == 3, "Failed save must restore app state" );
    require( warningShown, "Failed save must explain why the update was postponed" );
    saveSucceeds = true;
    require( [delegate updaterShouldRelaunchApplication:nil], "Save failure cannot wedge later attempts" );
    require( preparations == 4, "A later attempt must save again" );
    return 0;
}
