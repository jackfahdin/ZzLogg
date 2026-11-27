#include "relay_p.h"
#include "handofftiming.h"
#include "stablepackage_p.h"
#include "startupcheck_win.h"
#include "txcontract_win_p.h"
#include <filesystem>
#include <string>
namespace zzlogg::updater::detail {
namespace {
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
// Survives the GUI and temporary coordinator. No token or credential is
// written here. Engine logs remain in ProgramData beside the journal.
class UpdateLog {
public:
    explicit UpdateLog(const std::wstring& dataDirectory) {
        if(dataDirectory.empty())return;
        const auto directory=std::filesystem::path(dataDirectory)/L"logs";
        std::error_code error;std::filesystem::create_directories(directory,error);
        if(error)return;
        file_.reset(CreateFileW((directory/L"update-last.log").c_str(),GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    }
    void line(const std::string& text) {
        if(!file_)return;
        const auto record=std::to_string(GetTickCount64())+" "+text+"\r\n";
        DWORD written=0;WriteFile(file_.get(),record.data(),static_cast<DWORD>(record.size()),&written,nullptr);
        FlushFileBuffers(file_.get());
    }
    void peerExit(const ProcessIdentity& peer) {
        DWORD code=STILL_ACTIVE;
        if(peer.handle() && GetExitCodeProcess(peer.handle(),&code))line("peer_exit="+std::to_string(code));
    }
private:
    Handle file_;
};
int fail(LocalChannel& channel,Message message,int code) {
    message.kind=MessageKind::Failed;channel.send(message,after(timing::ControlMs));
    return code;
}
// A rolled-back transaction leaves the installation intact but the
// application already exited for the update; bring it back instead of
// leaving the user with a silently vanished program.
bool relaunchAfterFailedTransaction(const std::wstring& installRoot,const std::wstring& dataDirectory) {
    if(installRoot.empty())return false;
    const std::wstring image=installRoot+L"\\ZzLogg.exe";
    std::wstring command=quoteWindowsArgument(image);
    if(!dataDirectory.empty())command+=L" --data-dir "+quoteWindowsArgument(dataDirectory);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION launched{};
    if(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
        installRoot.c_str(),&startup,&launched)) {
        CloseHandle(launched.hThread);
        CloseHandle(launched.hProcess);
        // The restored release may predate the readiness event. Report launch
        // only; it must not be mislabeled as a successfully installed update.
        return true;
    }
    return false;
}
}
int runInstallRelay(ChildBootstrap& bootstrap,const CoordinatorOptions& options) {
    UpdateLog log(bootstrap.data().dataDirectory);
    std::string stage="bootstrap";
    log.line("stage="+stage);
    LocalChannel channel;const auto deadline=after(3000);
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),deadline))return BootstrapRejected;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,deadline))return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction
        || ready->token!=message.token || !bootstrap.parent().alive())return BootstrapRejected;
    const auto& data=bootstrap.data();
    if(!data.installerPath[0]) {log.line("result=failed stage=package code=execution_disabled");return fail(channel,message,ExecutionDisabled);}
    if(!packageUnchanged(data)) {log.line("result=failed stage=package code=package_rejected");return fail(channel,message,PackageRejected);}
    Coordinator coordinator;
    const auto failed=[&](int code) {
        log.peerExit(coordinator.process());
        log.line("result=failed stage="+stage+" code="+std::to_string(code));
        return fail(channel,message,code);
    };
    const auto enter=[&](const char* next) {stage=next;log.line("stage="+stage);};
    const InstallerRequest request{data.installerPath,data.installRoot,data.dataDirectory};
    const DirectoryIdentity* reserved=bootstrap.directoryReserved()?&data.directory:nullptr;
    LaunchError launch=LaunchError::Failed;
    enter("installer_launch");
    if(!coordinator.startInstaller(request,reserved,&bootstrap.parent(),options,&launch))
        return failed(
            launch==LaunchError::Cancelled?ElevationDeclined:InstallerLaunchFailed);
    enter("installer_authenticate");
    if(!coordinator.authenticate(after(timing::InstallerLaunchMs)))return failed(RelayFailed);
    enter("prepare");
    if(coordinator.awaitAppExit(after(timing::EnginePrepareMs))!=CoordinationResult::WaitingForAppExit)
        return failed(RelayFailed);
    // The engine is waiting: only now may the GUI be told to commit its exit.
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(timing::ControlMs)))return failed(RelayFailed);
    enter("commit_exit");
    const auto commit=channel.receive(after(timing::CommitExitMs));
    if(!commit || commit->kind!=MessageKind::CommitExit)return failed(RelayFailed);
    if(!coordinator.commitExit(after(timing::ControlMs)))return failed(RelayFailed);
    // Proceed is never released before the GUI process has really exited.
    enter("wait_application_exit");
    auto proceeded=CoordinationResult::PeerRunning;
    const auto exitDeadline=after(timing::ApplicationExitMs);
    while(remaining(exitDeadline) && proceeded==CoordinationResult::PeerRunning) {
        proceeded=coordinator.proceedIfExited(after(timing::ControlMs));
        if(proceeded==CoordinationResult::PeerRunning)Sleep(ProceedPollMs);
    }
    if(proceeded!=CoordinationResult::ProceedSent)return failed(RelayFailed);
    enter("install");
    const auto finished=coordinator.finish(after(timing::InstallMs));
    if(finished!=CoordinationResult::Complete) {
        DWORD engineCode=STILL_ACTIVE;
        if(finished!=CoordinationResult::ManualRestartRequired && coordinator.process().handle()
            && WaitForSingleObject(coordinator.process().handle(),timing::ControlMs)==WAIT_OBJECT_0
            && GetExitCodeProcess(coordinator.process().handle(),&engineCode)
            && (engineCode==exitForOutcome(TxOutcome::RolledBack)
                || engineCode==exitForOutcome(TxOutcome::Conflict)
                || engineCode==exitForOutcome(TxOutcome::Rejected))) {
            coordinator.releaseDirectoryObserver();
            bootstrap.releaseDirectoryObserver();
            enter("restore_restart");
            log.line(relaunchAfterFailedTransaction(data.installRoot,data.dataDirectory)
                ?"restored_application=launched":"restored_application=restart_failed");
        }
        log.peerExit(coordinator.process());
        log.line(finished==CoordinationResult::ManualRestartRequired
            ?"result=manual_restart_required":"result=failed stage="+stage);
        return finished==CoordinationResult::ManualRestartRequired?RestartPending:RelayFailed;
    }
    // This bootstrap owns a second observer independently of Coordinator.
    // Both handles must close after confirmed completion and before restart.
    bootstrap.releaseDirectoryObserver();
    enter("restart");
    const bool restarted=coordinator.restart(after(timing::RestartMs))==CoordinationResult::Restarted;
    log.line(restarted?"result=complete":"result=installed_restart_failed");
    return restarted?0:RestartPending;
}
}
