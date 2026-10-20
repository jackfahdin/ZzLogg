#include "macupdatecontroller.h"
#include "macupdatecontroller_p.h"

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

#include <QDebug>
#include <QMessageBox>
#include <utility>

@implementation ZzLoggSparkleDelegate
- (BOOL)updaterShouldRelaunchApplication:(SPUUpdater*)updater
{
    (void)updater;
    // Sparkle invokes this veto before allowing its installer to terminate the
    // host. Saving from updaterWillRelaunchApplication would be too late to veto.
    // The application's native Quit event commits this prepared exit.
    if ( exitPrepared ) return YES;
    if ( prepareExit && prepareExit() ) {
        exitPrepared = true;
        return YES;
    }
    if ( cancelExit ) cancelExit();
    QMessageBox::warning( nullptr, QObject::tr( "Update postponed" ),
        QObject::tr( "The session could not be saved. The application will stay open. "
                     "Resolve the save problem, then check for updates again." ) );
    return NO;
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error
{
    (void)updater;
    (void)error;
    if ( exitPrepared && cancelExit ) cancelExit();
    exitPrepared = false;
}
@end

struct MacUpdateController::Impl {
    ZzLoggSparkleDelegate* delegate = nil;
    SPUStandardUpdaterController* controller = nil;
    bool started = false;
};

MacUpdateController::MacUpdateController(
    std::function<bool()> prepareApplicationExit,
    std::function<void()> cancelApplicationExitPreparation, QObject* parent )
    : QObject( parent ), impl_( std::make_unique<Impl>() )
{
    Q_ASSERT( [NSThread isMainThread] );
    impl_->delegate = [[ZzLoggSparkleDelegate alloc] init];
    impl_->delegate->prepareExit = std::move( prepareApplicationExit );
    impl_->delegate->cancelExit = std::move( cancelApplicationExitPreparation );
    impl_->controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:NO updaterDelegate:impl_->delegate userDriverDelegate:nil];
    NSError* error = nil;
    impl_->started = [impl_->controller.updater startUpdater:&error];
    if ( !impl_->started ) {
        qWarning() << "Cannot start macOS updater:" << error.localizedDescription.UTF8String;
    }
}

MacUpdateController::~MacUpdateController() = default;

bool MacUpdateController::available() const
{
    return impl_->started;
}

void MacUpdateController::checkForUpdates( bool manual )
{
    Q_ASSERT( [NSThread isMainThread] );
    if ( !available() ) return;
    if ( manual ) {
        // The standard UI also brings an existing update session to the front.
        [impl_->controller checkForUpdates:nil];
    }
    else if ( impl_->controller.updater.canCheckForUpdates ) {
        [impl_->controller.updater checkForUpdatesInBackground];
    }
}
