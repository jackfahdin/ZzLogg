#include "relay_p.h"
#include "stablepackage_p.h"
#include <string>
namespace zzlogg::updater::detail {
namespace {
// The UAC prompt and the installer's own extraction both happen inside this
// budget; the engine only appears once the user accepted the elevation.
constexpr DWORD LaunchBudgetMs=300000;
// Engine prepare is a read-only scan, but it covers the whole installation.
constexpr DWORD PrepareBudgetMs=180000;
constexpr DWORD ShortBudgetMs=5000;
// Transaction execution plus the registration writes; exceeding this reports a
// failure instead of waiting without a bound.
constexpr DWORD FinishBudgetMs=600000;
constexpr DWORD RestartBudgetMs=60000;
// Proceed gate poll interval; the gate itself is the application's real exit.
constexpr DWORD ProceedPollMs=50;

std::string hexDigest(const uint8_t (&digest)[32]) {
    static constexpr char hex[]="0123456789abcdef";
    std::string text;text.reserve(64);
    for(const auto byte:digest){text+=hex[byte>>4];text+=hex[byte&15];}
    return text;
}
// The coordinator rechecks the package bytes on its own: the parent's
// verification verdict is never evidence.
bool packageUnchanged(const BootstrapData& data) {
    zzlogg::update::detail::StablePackage lease;
    if(lease.open(data.installerPath)!=zzlogg::update::PackageVerificationError::None)return false;
    return lease.verifyContent(data.packageSize,hexDigest(data.packageSha256))
        ==zzlogg::update::PackageVerificationError::None;
}
int fail(LocalChannel& channel,Message message,int code) {
    message.kind=MessageKind::Failed;channel.send(message,after(ShortBudgetMs));
    return code;
}
}
int runInstallRelay(ChildBootstrap& bootstrap,const CoordinatorOptions& options) {
    LocalChannel channel;const auto deadline=after(3000);
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),deadline))return BootstrapRejected;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,deadline))return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction
        || ready->token!=message.token || !bootstrap.parent().alive())return BootstrapRejected;
    const auto& data=bootstrap.data();
    if(!data.installerPath[0])return fail(channel,message,ExecutionDisabled);
    if(!packageUnchanged(data))return fail(channel,message,PackageRejected);
    Coordinator coordinator;
    const InstallerRequest request{data.installerPath,data.installRoot,data.dataDirectory};
    const DirectoryIdentity* reserved=bootstrap.directoryReserved()?&data.directory:nullptr;
    LaunchError launch=LaunchError::Failed;
    if(!coordinator.startInstaller(request,reserved,&bootstrap.parent(),options,&launch))
        return fail(channel,message,
            launch==LaunchError::Cancelled?ElevationDeclined:InstallerLaunchFailed);
    if(!coordinator.authenticate(after(LaunchBudgetMs)))return fail(channel,message,RelayFailed);
    if(coordinator.awaitAppExit(after(PrepareBudgetMs))!=CoordinationResult::WaitingForAppExit)
        return fail(channel,message,RelayFailed);
    // The engine is waiting: only now may the GUI be told to commit its exit.
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(ShortBudgetMs)))return fail(channel,message,RelayFailed);
    const auto commit=channel.receive(after(LaunchBudgetMs));
    if(!commit || commit->kind!=MessageKind::CommitExit)return fail(channel,message,RelayFailed);
    if(!coordinator.commitExit(after(ShortBudgetMs)))return fail(channel,message,RelayFailed);
    // Proceed is never released before the GUI process has really exited.
    auto proceeded=CoordinationResult::PeerRunning;
    for(DWORD waited=0;waited<LaunchBudgetMs && proceeded==CoordinationResult::PeerRunning;waited+=ProceedPollMs) {
        proceeded=coordinator.proceedIfExited(after(ShortBudgetMs));
        if(proceeded==CoordinationResult::PeerRunning)Sleep(ProceedPollMs);
    }
    if(proceeded!=CoordinationResult::ProceedSent)return fail(channel,message,RelayFailed);
    const auto finished=coordinator.finish(after(FinishBudgetMs));
    if(finished!=CoordinationResult::Complete)
        return finished==CoordinationResult::ManualRestartRequired?RestartPending:RelayFailed;
    return coordinator.restart(after(RestartBudgetMs))==CoordinationResult::Restarted?0:RestartPending;
}
}
