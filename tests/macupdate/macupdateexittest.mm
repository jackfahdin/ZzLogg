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
    int completedCycles = 0;
    bool saveSucceeds = true;
    auto* delegate = [[ZzLoggSparkleDelegate alloc] init];
    delegate->prepareExit = [&] { ++preparations; return saveSucceeds; };
    delegate->cancelExit = [&] { ++cancellations; };
    delegate->onCycleFinished = [&] { ++completedCycles; };
    auto require = []( bool condition, const char* message ) {
        if ( !condition ) qFatal( "%s", message );
    };

#if defined(__arm64__) || defined(__aarch64__)
    NSString* stableFeed = @"https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-arm64-stable.xml";
    NSString* previewFeed = @"https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-arm64-preview.xml";
#else
    NSString* stableFeed = @"https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-x64-stable.xml";
    NSString* previewFeed = @"https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-x64-preview.xml";
#endif
    [delegate setUpdateChannel:@"stable"];
    require( [[delegate feedURLStringForUpdater:nil] isEqualToString:stableFeed],
             "Stable must select the stable Sparkle feed" );
    [delegate setUpdateChannel:@"preview"];
    require( [[delegate feedURLStringForUpdater:nil] isEqualToString:previewFeed],
             "Preview must select the preview Sparkle feed" );
    [delegate setUpdateChannel:@"stable"];
    require( [[delegate feedURLStringForUpdater:nil] isEqualToString:stableFeed],
             "Switching back must restore the stable Sparkle feed" );

    NSError* checkError = nil;
    require( [delegate updater:nil mayPerformUpdateCheck:SPUUpdateCheckUpdates error:&checkError],
             "Stable check must be allowed" );
    [delegate setUpdateChannel:@"preview"];
    NSError* staleError = nil;
    require( ![delegate updater:nil shouldProceedWithUpdate:nil updateCheck:SPUUpdateCheckUpdates error:&staleError]
                 && staleError != nil,
             "An old-channel result must not be offered after switching" );
    bool channelWarningShown = false;
    QTimer::singleShot( 0, [&] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* warning = qobject_cast<QMessageBox*>( widget ) ) {
                channelWarningShown = true;
                warning->accept();
            }
        }
    } );
    require( ![delegate updaterShouldRelaunchApplication:nil],
             "An already displayed old-channel update must not install" );
    require( channelWarningShown && preparations == 0,
             "Rejecting a stale update must not prepare application exit" );
    [delegate updater:nil didFinishUpdateCycleForUpdateCheck:SPUUpdateCheckUpdates error:nil];
    require( completedCycles == 1, "Finishing a stale cycle must notify the controller" );
    require( ![delegate hasStaleUpdateSession], "A completed stale session must release its channel" );
    require( [delegate updater:nil mayPerformUpdateCheck:SPUUpdateCheckUpdates error:&checkError],
             "A new check on the selected channel must be allowed" );
    require( [delegate updater:nil shouldProceedWithUpdate:nil updateCheck:SPUUpdateCheckUpdates error:&staleError],
             "A new-channel result may proceed" );
    [delegate updater:nil didFinishUpdateCycleForUpdateCheck:SPUUpdateCheckUpdates error:nil];
    [delegate setUpdateChannel:@"stable"];
    bool offeredWarningShown = false;
    QTimer::singleShot( 0, [&] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* warning = qobject_cast<QMessageBox*>( widget ) ) {
                offeredWarningShown = true;
                warning->accept();
            }
        }
    } );
    require( ![delegate updaterShouldRelaunchApplication:nil],
             "A previously offered update cannot install after changing channels" );
    require( offeredWarningShown && preparations == 0,
             "Blocking a previously offered update must not prepare application exit" );
    require( [delegate updater:nil mayPerformUpdateCheck:SPUUpdateCheckUpdates error:&checkError],
             "A stable check may start after a preview offer" );
    require( [delegate updater:nil shouldProceedWithUpdate:nil updateCheck:SPUUpdateCheckUpdates error:&staleError],
             "A stable offer may replace the preview offer" );
    [delegate updater:nil didFinishUpdateCycleForUpdateCheck:SPUUpdateCheckUpdates error:nil];
    require( completedCycles == 3, "Each completed check must notify the controller once" );

    auto* replacedDelegate = [[ZzLoggSparkleDelegate alloc] init];
    int replacedPreparations = 0;
    replacedDelegate->prepareExit = [&] { ++replacedPreparations; return true; };
    SPUUpdater* oldUpdater = (SPUUpdater*)[[NSObject alloc] init];
    SPUUpdater* newUpdater = (SPUUpdater*)[[NSObject alloc] init];
    [replacedDelegate setUpdateChannel:@"stable"];
    [replacedDelegate setActiveUpdater:oldUpdater];
    require( [replacedDelegate updater:oldUpdater mayPerformUpdateCheck:SPUUpdateCheckUpdates error:nil],
             "The active updater may start a check" );
    require( [replacedDelegate updater:oldUpdater shouldProceedWithUpdate:nil
        updateCheck:SPUUpdateCheckUpdates error:nil], "The active updater may offer an update" );
    [replacedDelegate updater:oldUpdater didFinishUpdateCycleForUpdateCheck:SPUUpdateCheckUpdates error:nil];
    [replacedDelegate setUpdateChannel:@"preview"];
    [replacedDelegate setActiveUpdater:newUpdater];
    require( ![replacedDelegate updater:oldUpdater mayPerformUpdateCheck:SPUUpdateCheckUpdates error:nil],
             "A replaced updater must not start another check" );
    require( ![replacedDelegate updater:oldUpdater shouldProceedWithUpdate:nil
        updateCheck:SPUUpdateCheckUpdates error:nil], "A replaced updater must not offer an update" );
    QTimer::singleShot( 0, [&] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* warning = qobject_cast<QMessageBox*>( widget ) ) warning->accept();
        }
    } );
    require( ![replacedDelegate updaterShouldRelaunchApplication:oldUpdater],
             "A replaced updater must not install its deferred update" );
    require( replacedPreparations == 0, "A replaced updater must not prepare application exit" );

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
