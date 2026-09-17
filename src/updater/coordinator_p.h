#pragma once
#include "bootstrap_win_p.h"
#include <functional>
#include <memory>
namespace zzlogg::updater::detail {
enum class CoordinationResult { Failed, WaitingForAppExit, PeerRunning, ProceedSent, Complete, ManualRestartRequired, Cancelled,
    Restarted, RestartFailed };
// Installer launch outcome: Cancelled is the UAC refusal mapping
// (ERROR_CANCELLED), Failed every other launch failure.
enum class LaunchError { None, Cancelled, Failed };
struct LauncherOutcome { LaunchError error=LaunchError::Failed;ProcessIdentity process; };
// Installer launch seam. Production composes ShellExecuteEx runas; dedicated
// test targets inject a fixture launcher. The switch argument is exactly
// /ZzLoggUpgrade=<locator>; no other parameter ever reaches the command line.
using InstallerLauncher=std::function<LauncherOutcome(const std::wstring& installer,const std::wstring& restrictedSwitch)>;
struct CoordinatorOptions {
    InstallerLauncher launcher;      // empty: production ShellExecuteEx runas
    bool requireElevatedPeer=true;   // engine client elevation proof (production)
};
struct InstallerRequest {
    std::wstring installer;      // verified installer package path (3B.2 output)
    std::wstring installRoot;    // registered installation directory
    std::wstring dataDirectory;  // optional bootstrap v3 restart context
};
// Production launcher: ShellExecuteEx + runas with only the restricted
// switch. ERROR_CANCELLED maps to LaunchError::Cancelled. Never invoked on
// this machine's tests; the mapping is covered through the injected seam.
LauncherOutcome shellExecuteElevated(const std::wstring& installer,const std::wstring& restrictedSwitch);
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
    // 3C installer chain: writes the current-user-private credential file
    // (random locator-named temp file), launches the verified installer
    // through the launcher seam with only the restricted switch, holds the
    // reservation observer across the application's exit, and pins the
    // installation identity for the post-transaction restart recheck. The
    // channel client authenticated later is the engine grandchild, never the
    // installer process itself.
    bool startInstaller(const InstallerRequest&,const DirectoryIdentity* reservedIdentity,
        const ProcessIdentity* application,const CoordinatorOptions&,LaunchError* error=nullptr);
    bool authenticate(Deadline);
    CoordinationResult awaitAppExit(Deadline);
    bool canCommitExit() const;
    bool commitExit(Deadline);
    // Proceed gate: after commitExit, sends Proceed only once the application
    // process has really exited. PeerRunning while it lives; ProceedSent once
    // the gate opened (idempotent); fail-closed without an application handle.
    CoordinationResult proceedIfExited(Deadline);
    CoordinationResult finish(Deadline);
    // After finish()==Complete (never ManualRestartRequired): rechecks that
    // <installRoot> is still the pinned registered directory with a
    // consistent marker/manifest, restarts ZzLogg.exe as the current
    // (original) user carrying only --data-dir, and boundedly requires the
    // process to stay alive with its directory-scoped single-instance
    // endpoint present. Confirmation failure reports RestartFailed and keeps
    // the transaction backup for authorized recovery.
    CoordinationResult restart(Deadline);
    void cancel(Deadline);
    const ProcessIdentity& process() const;
    const std::wstring& runtimePath() const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
