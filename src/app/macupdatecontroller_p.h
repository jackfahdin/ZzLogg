#ifndef ZZLOGG_MACUPDATECONTROLLER_P_H
#define ZZLOGG_MACUPDATECONTROLLER_P_H

#import <Foundation/Foundation.h>
#import <Sparkle/SPUUpdaterDelegate.h>
#include <functional>

// Private Objective-C++ delegate. The native tests exercise the same veto and
// recovery callbacks that Sparkle invokes, without downloading an update.
@interface ZzLoggSparkleDelegate : NSObject <SPUUpdaterDelegate> {
@public
    std::function<bool()> prepareExit;
    std::function<void()> cancelExit;
    bool exitPrepared;
}
@end

#endif
