#pragma once
// Install coordination relay: ZzLoggUpdate.exe outlives the GUI, so the
// proceedIfExited release decision can only be taken here. The production
// entry point (main.cpp) and the test stand-in entry point run this same
// implementation; the only difference is the CoordinatorOptions passed in.
#include "bootstrap_win_p.h"
#include "coordinator_p.h"
namespace zzlogg::updater::detail {
// Completes the parent handshake, rechecks the package bytes and drives the
// whole installation chain. Returns the process exit code: 0 = installed and
// restarted, everything else is a coordinator exit code from bootstrap_win_p.h.
int runInstallRelay(ChildBootstrap& bootstrap,const CoordinatorOptions& options);
}
