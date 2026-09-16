#pragma once
#include "bootstrap_win_p.h"
#include <memory>
namespace zzlogg::updater::detail {
enum class CoordinationResult { Failed, WaitingForAppExit, PeerRunning, Complete, ManualRestartRequired, Cancelled };
// Internal ordinary-user composition. A path here is a coordinator/fixture,
// never a VerifiedPackage or authority to execute an installer.
class Coordinator {
public:
    // Destruction disconnects immediately; a private owner retains a live
    // process and runtime leases until actual exit. No UI-thread wait/kill.
    Coordinator();~Coordinator();
    bool start(const std::wstring& coordinator,const std::wstring& runtimeBase);
    bool authenticate(Deadline);
    CoordinationResult awaitAppExit(Deadline);
    bool canCommitExit() const;
    bool commitExit(Deadline);
    CoordinationResult finish(Deadline);
    void cancel(Deadline);
    const ProcessIdentity& process() const;
    const std::wstring& runtimePath() const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
