#include "macupdatecontroller.h"
#include "macupdatecontroller_p.h"

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <utility>

@implementation ZzLoggSparkleDelegate {
    NSString* feedURLString_;
    NSString* selectedChannel_;
    NSString* sessionChannel_;
    NSString* offeredChannel_;
    __weak SPUUpdater* activeUpdater_;
}
- (BOOL)setUpdateChannel:(NSString*)channel
{
    NSString* selected = [channel isEqualToString:@"preview"] ? @"preview" : @"stable";
    BOOL changed = ![selectedChannel_ isEqualToString:selected];
    selectedChannel_ = selected;
    NSString* prefix = [NSString stringWithUTF8String:ZZLOGG_SPARKLE_FEED_PREFIX];
    feedURLString_ = [prefix stringByAppendingFormat:@"%@.xml", selectedChannel_];
    return changed;
}

- (void)setActiveUpdater:(SPUUpdater*)updater
{
    if ( exitPrepared && cancelExit ) cancelExit();
    exitPrepared = false;
    activeUpdater_ = updater;
    sessionChannel_ = nil;
    offeredChannel_ = nil;
}

- (BOOL)hasStaleUpdateSession
{
    return sessionChannel_ != nil && ![sessionChannel_ isEqualToString:selectedChannel_];
}

- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater
{
    (void)updater;
    return feedURLString_;
}

- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck
         error:(NSError* __autoreleasing*)error
{
    (void)updateCheck;
    if ( updater != activeUpdater_ ) {
        if ( error ) *error = [NSError errorWithDomain:@"ZzLoggSparkleChannel" code:2
            userInfo:@{NSLocalizedDescriptionKey:@"This update checker was replaced."}];
        return NO;
    }
    sessionChannel_ = selectedChannel_;
    return YES;
}

- (BOOL)updater:(SPUUpdater*)updater shouldProceedWithUpdate:(SUAppcastItem*)updateItem
    updateCheck:(SPUUpdateCheck)updateCheck error:(NSError* __autoreleasing*)error
{
    (void)updateItem;
    (void)updateCheck;
    if ( updater != activeUpdater_ || [self hasStaleUpdateSession] ) {
        if ( error ) {
            *error = [NSError errorWithDomain:@"ZzLoggSparkleChannel" code:1
                userInfo:@{NSLocalizedDescriptionKey:
                    @"The update channel changed during this check. Check for updates again."}];
        }
        return NO;
    }
    offeredChannel_ = selectedChannel_;
    return YES;
}

- (void)updater:(SPUUpdater*)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
         error:(NSError*)error
{
    (void)updateCheck;
    (void)error;
    if ( updater != activeUpdater_ ) return;
    sessionChannel_ = nil;
    if ( onCycleFinished ) onCycleFinished();
}

- (BOOL)updaterShouldRelaunchApplication:(SPUUpdater*)updater
{
    if ( updater != activeUpdater_ || [self hasStaleUpdateSession]
         || ( offeredChannel_ != nil && ![offeredChannel_ isEqualToString:selectedChannel_] ) ) {
        if ( exitPrepared && cancelExit ) cancelExit();
        exitPrepared = false;
        QMessageBox::warning( nullptr, QObject::tr( "Update postponed" ),
            QObject::tr( "The update channel changed after this update was found. "
                         "Close the current update, then check for updates again." ) );
        return NO;
    }
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
    (void)error;
    if ( updater != activeUpdater_ ) return;
    if ( exitPrepared && cancelExit ) cancelExit();
    exitPrepared = false;
}
@end

struct MacUpdateController::Impl {
    ZzLoggSparkleDelegate* delegate = nil;
    SPUStandardUpdaterController* controller = nil;
    bool started = false;
    bool resetRequested = false;
    bool restartingForChannel = false;
    bool pendingCheck = false;
    bool pendingManualCheck = false;
};

MacUpdateController::MacUpdateController(
    const QString& channel, std::function<bool()> prepareApplicationExit,
    std::function<void()> cancelApplicationExitPreparation, QObject* parent )
    : QObject( parent ), impl_( std::make_unique<Impl>() )
{
    Q_ASSERT( [NSThread isMainThread] );
    impl_->delegate = [[ZzLoggSparkleDelegate alloc] init];
    setChannel( channel );
    impl_->delegate->prepareExit = std::move( prepareApplicationExit );
    impl_->delegate->cancelExit = std::move( cancelApplicationExitPreparation );
    impl_->delegate->onCycleFinished = [this] {
        if ( impl_->resetRequested && !impl_->restartingForChannel ) {
            impl_->restartingForChannel = true;
            QTimer::singleShot( 0, this, [this] {
                // Sparkle retains a deferred download in its updater. A fresh
                // updater must be used so this check actually requests the new feed.
                const bool pending = std::exchange( impl_->pendingCheck, false );
                const bool manual = std::exchange( impl_->pendingManualCheck, false );
                impl_->restartingForChannel = false;
                if ( !restartUpdater() ) {
                    if ( pending && manual ) QMessageBox::warning( nullptr, tr( "Update postponed" ),
                        tr( "The update checker could not restart for the selected channel. "
                            "Try checking for updates again." ) );
                    return;
                }
                if ( pending ) checkForUpdates( manual );
            } );
        }
    };
    impl_->controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:NO updaterDelegate:impl_->delegate userDriverDelegate:nil];
    [impl_->delegate setActiveUpdater:impl_->controller.updater];
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

void MacUpdateController::setChannel( const QString& channel )
{
    Q_ASSERT( [NSThread isMainThread] );
    const bool changed = [impl_->delegate setUpdateChannel:
        [NSString stringWithUTF8String:channel.toUtf8().constData()]];
    if ( !changed || !impl_->started ) return;
    impl_->resetRequested = true;
    if ( !impl_->restartingForChannel && !impl_->controller.updater.sessionInProgress ) restartUpdater();
}

bool MacUpdateController::restartUpdater()
{
    impl_->restartingForChannel = true;
    impl_->started = false;
    impl_->controller = nil;
    impl_->controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:NO updaterDelegate:impl_->delegate userDriverDelegate:nil];
    [impl_->delegate setActiveUpdater:impl_->controller.updater];
    NSError* error = nil;
    impl_->started = [impl_->controller.updater startUpdater:&error];
    impl_->resetRequested = false;
    impl_->restartingForChannel = false;
    if ( !impl_->started )
        qWarning() << "Cannot restart macOS updater:" << error.localizedDescription.UTF8String;
    return impl_->started;
}

void MacUpdateController::checkForUpdates( bool manual )
{
    Q_ASSERT( [NSThread isMainThread] );
    if ( impl_->restartingForChannel ) {
        impl_->pendingCheck = true;
        impl_->pendingManualCheck |= manual;
        return;
    }
    if ( !available() ) return;
    if ( impl_->resetRequested ) {
        if ( impl_->controller.updater.sessionInProgress ) {
            impl_->pendingCheck = true;
            impl_->pendingManualCheck |= manual;
            return;
        }
        if ( !restartUpdater() ) return;
    }
    impl_->pendingCheck = false;
    impl_->pendingManualCheck = false;
    if ( manual ) {
        // The standard UI also brings an existing update session to the front.
        [impl_->controller checkForUpdates:nil];
    }
    else if ( impl_->controller.updater.canCheckForUpdates ) {
        [impl_->controller.updater checkForUpdatesInBackground];
    }
}
