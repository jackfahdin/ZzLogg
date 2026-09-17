#pragma once
#include "bootstrap_win_p.h"
#include <memory>
namespace zzlogg::updater::detail {
enum class CoordinationResult { Failed, WaitingForAppExit, PeerRunning, ProceedSent, Complete, ManualRestartRequired, Cancelled };
// Internal ordinary-user composition. A path here is a coordinator/fixture,
// never a VerifiedPackage or authority to execute an installer.
class Coordinator {
public:
    // Destruction disconnects immediately; a private owner retains a live
    // process and runtime leases until actual exit. No UI-thread wait/kill.
    Coordinator();~Coordinator();
    // When reservedIdentity is present, the bootstrap carries the reserved
    // directory identity and the child binds an observer handle to the
    // existing mutex before Hello, keeping the gate alive across this
    // process's exit. The reservation itself stays owned by the caller.
    // When application is present, the coordinator duplicates its handle and
    // proceedIfExited gates the transaction Proceed on that process's real
    // exit. dataDirectory (optional, absolute, bounded) rides the bootstrap
    // mapping for the post-transaction restart.
    bool start(const std::wstring& coordinator,const std::wstring& runtimeBase,
        const DirectoryIdentity* reservedIdentity=nullptr,
        const ProcessIdentity* application=nullptr,const std::wstring& dataDirectory={});
    bool authenticate(Deadline);
    CoordinationResult awaitAppExit(Deadline);
    bool canCommitExit() const;
    bool commitExit(Deadline);
    // Proceed gate: after commitExit, sends Proceed only once the application
    // process has really exited. PeerRunning while it lives; ProceedSent once
    // the gate opened (idempotent); fail-closed without an application handle.
    CoordinationResult proceedIfExited(Deadline);
    CoordinationResult finish(Deadline);
    void cancel(Deadline);
    const ProcessIdentity& process() const;
    const std::wstring& runtimePath() const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
