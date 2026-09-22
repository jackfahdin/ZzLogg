#include "coordinator_p.h"
#include "installationactivity_win.h"
#include <shellapi.h>
#include <cstring>
#include <thread>
namespace zzlogg::updater::detail {
namespace {
// Marker/manifest consistency for the restart recheck: bounded prefix match,
// never a full parse. The marker carries "ZzLogg <version>", the manifest
// opens with the ZZTXMAN1 magic.
bool filePrefix(const std::wstring& path,const char* prefix) {
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,0,nullptr));
    if(!file)return false;
    char buffer[64]{};DWORD read=0;const auto length=std::strlen(prefix);
    return ReadFile(file.get(),buffer,sizeof(buffer),&read,nullptr) && read>=length
        && std::memcmp(buffer,prefix,length)==0;
}
bool filePresent(const std::wstring& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
}
struct Coordinator::Impl {
    RuntimeCopy copy;ProcessIdentity process;LocalChannel channel;
    TransactionId transaction{};SessionToken token{};std::unique_ptr<HandoffSession> session;
    ProcessIdentity application;
    bool elevated=false,failed=false,completeReceived=false,applicationHeld=false,proceedSent=false;
    // Installer-chain state (startInstaller composition only).
    bool installerChain=false,requireElevatedPeer=true,finishedOk=false;
    std::wstring installRoot,dataDirectory,credential;
    DirectoryIdentity installIdentity{};
    Handle directoryObserver;
    ProcessIdentity restarted;
    void abort(){
        failed=true;channel.close();
        // Leftover credentials are never left behind on failure paths; after
        // a successful engine read the file is already gone.
        if(!credential.empty()){deleteCredentialFile(credential);credential.clear();}
    }
    Message message(MessageKind kind) const{return {kind,transaction,token};}
};
LauncherOutcome shellExecuteElevated(const std::wstring& installer,const std::wstring& restrictedSwitch){
    LauncherOutcome outcome;outcome.error=LaunchError::Failed;
    SHELLEXECUTEINFOW execution{};execution.cbSize=sizeof(execution);
    execution.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    execution.lpVerb=L"runas";execution.lpFile=installer.c_str();execution.lpParameters=restrictedSwitch.c_str();
    execution.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&execution)) {
        outcome.error=GetLastError()==ERROR_CANCELLED?LaunchError::Cancelled:LaunchError::Failed;
        return outcome;
    }
    Handle launched(execution.hProcess);
    // Identity-verification window: ShellExecuteEx already succeeded, so if
    // any step below (GetProcessTimes/DuplicateHandle/adopt) fails, the
    // elevated installer runs orphaned until its own exit. The chain stays
    // fail-closed throughout: the coordinator reports the launch failure,
    // the credential file is deleted on abort, and the engine grandchild can
    // never authenticate without them. Accepted residual risk (3C deferred
    // triage #23); the window is three bounded API calls, not user time.
    FILETIME created{},exited{},kernel{},user{};
    if(!launched || !GetProcessTimes(launched.get(),&created,&exited,&kernel,&user))return outcome;
    const uint64_t stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),launched.get(),GetCurrentProcess(),&limited,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return outcome;
    ProcessIdentity identity;
    if(!identity.adopt(limited,{GetProcessId(launched.get()),stamp}))return outcome;
    outcome.process=std::move(identity);outcome.error=LaunchError::None;return outcome;
}
Coordinator::Coordinator():impl_(std::make_unique<Impl>()){}
Coordinator::~Coordinator(){
    impl_->channel.close();
    // A credential the engine never consumed is useless once this process is
    // gone (its identity check fails closed) and is never left as litter.
    if(!impl_->credential.empty()){deleteCredentialFile(impl_->credential);impl_->credential.clear();}
    if(!impl_->process.alive())return;
    // Cancellation must return control to the caller without releasing a live
    // child's leases. A private reaper owns only handles/files, never a UI or mutex.
    auto pending=impl_.release();
    try {std::thread([pending]{
        if(WaitForSingleObject(pending->process.handle(),INFINITE)==WAIT_OBJECT_0)delete pending;
    }).detach();}
    catch(...) {
        // Thread resource exhaustion: retain handles until process teardown.
        // A diagnosable residue is safer than releasing/cleaning a live image.
    }
}
bool Coordinator::start(const std::wstring& source,const std::wstring& base,const DirectoryIdentity* reservedIdentity,
    const ProcessIdentity* application,const std::wstring& dataDirectory){
    HandoffRequest request;request.dataDirectory=dataDirectory;
    return start(source,base,reservedIdentity,application,request);
}
bool Coordinator::start(const std::wstring& source,const std::wstring& base,const DirectoryIdentity* reservedIdentity,
    const ProcessIdentity* application,const HandoffRequest& request){
    if(impl_->session)return false;ProcessIdentity self;
    if(reservedIdentity && !reservedIdentity->volumeSerial)return false;
    if(application && !application->handle())return false;
    if(!self.open(GetCurrentProcessId()) || !randomBytes(impl_->transaction.data(),16) || !randomBytes(impl_->token.data(),32))return false;
    if(application){
        HANDLE raw=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),application->handle(),GetCurrentProcess(),&raw,
            PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return false;
        // adopt keeps ownership even when authentication fails; a failed adopt
        // leaves the stamp empty, which can never pass the gate.
        if(!impl_->application.adopt(raw,application->stamp()))return false;
        impl_->applicationHeld=true;
    }
    impl_->elevated=self.elevated();impl_->session=std::make_unique<HandoffSession>(impl_->transaction,impl_->token);
    ChildLaunchRequest launch;
    launch.transaction=impl_->transaction;launch.token=impl_->token;launch.reservedIdentity=reservedIdentity;
    launch.dataDirectory=request.dataDirectory;launch.installerPath=request.installerPath;
    launch.installRoot=request.installRoot;launch.packageSize=request.packageSize;
    launch.packageSha256=request.packageSha256;
    if(!impl_->copy.create(source,base) || !impl_->channel.create(impl_->transaction)
        || !launchCopy(impl_->copy,launch,impl_->process)){impl_->abort();return false;}return true;
}
bool Coordinator::startInstaller(const InstallerRequest& request,const DirectoryIdentity* reservedIdentity,
    const ProcessIdentity* application,const CoordinatorOptions& options,LaunchError* error){
    if(error)*error=LaunchError::Failed;
    if(impl_->session || request.installer.empty() || request.installRoot.empty()
        || request.dataDirectory.size()>=PathCapacity
        || !absolutePathPlausible(request.dataDirectory.data(),request.dataDirectory.size()))return false;
    if(reservedIdentity && !reservedIdentity->volumeSerial)return false;
    if(application && !application->handle())return false;
    ProcessIdentity self;
    if(!self.open(GetCurrentProcessId()))return false;
    // The locator doubles as the journal txid and the credential file name;
    // it must satisfy the engine's parseTxid acceptance exactly.
    do {
        if(!randomBytes(impl_->transaction.data(),16))return false;
        impl_->credential=credentialLocator(impl_->transaction);
    } while(impl_->credential.empty());
    if(!randomBytes(impl_->token.data(),32))return false;
    if(application){
        HANDLE raw=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),application->handle(),GetCurrentProcess(),&raw,
            PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return false;
        if(!impl_->application.adopt(raw,application->stamp()))return false;
        impl_->applicationHeld=true;
    }
    impl_->elevated=self.elevated();
    impl_->installerChain=true;impl_->requireElevatedPeer=options.requireElevatedPeer;
    impl_->installRoot=request.installRoot;impl_->dataDirectory=request.dataDirectory;
    // Pin the registered directory identity for the restart recheck: the
    // caller's reservation identity when present, otherwise a direct probe.
    if(reservedIdentity)impl_->installIdentity=*reservedIdentity;
    else if(!InstallationActivity::probeIdentity(request.installRoot,impl_->installIdentity))return false;
    if(reservedIdentity){
        // Observer on the existing reservation mutex: the gate stays closed
        // across the application's real exit until this composition ends.
        const auto name=InstallLock::mutexName(*reservedIdentity);
        if(name.empty())return false;
        Handle observer(OpenMutexW(SYNCHRONIZE,FALSE,name.c_str()));
        if(!observer)return false;
        impl_->directoryObserver=std::move(observer);
    }
    impl_->session=std::make_unique<HandoffSession>(impl_->transaction,impl_->token);
    if(!impl_->channel.create(impl_->transaction)){impl_->abort();return false;}
    CredentialData credential;
    credential.coordinatorPid=self.stamp().pid;credential.coordinatorCreated=self.stamp().created;
    credential.transaction=impl_->transaction;credential.token=impl_->token;
    if(!writeCredentialFile(credential)){impl_->abort();return false;}
    auto outcome=(options.launcher?options.launcher:shellExecuteElevated)(request.installer,
        L"/ZzLoggUpgrade="+impl_->credential);
    if(outcome.error!=LaunchError::None){
        if(error)*error=outcome.error;
        impl_->abort();return false;
    }
    // The installer process is tracked only until the engine grandchild
    // authenticates; afterwards process() is the engine.
    impl_->process=std::move(outcome.process);
    if(error)*error=LaunchError::None;
    return true;
}
bool Coordinator::authenticate(Deadline deadline){
    if(!impl_->session || impl_->failed || impl_->session->state()!=HandoffState::Connecting){impl_->abort();return false;}
    if(impl_->installerChain){
        ProcessIdentity client;
        if(!impl_->channel.acceptClient(deadline,client)
            || (impl_->requireElevatedPeer && !client.elevated())){impl_->abort();return false;}
        impl_->process=std::move(client);
    }
    else if(!impl_->channel.accept(impl_->process,deadline)){impl_->abort();return false;}
    auto message=impl_->channel.receive(deadline);
    if(!message || message->kind!=MessageKind::Hello || !impl_->session->accept(*message) || !impl_->process.alive()
        || !impl_->channel.send(impl_->message(MessageKind::Ready),deadline)){impl_->abort();return false;}return true;
}
CoordinationResult Coordinator::awaitAppExit(Deadline deadline){
    if(!impl_->session || impl_->failed || impl_->session->state()!=HandoffState::Ready){impl_->abort();return CoordinationResult::Failed;}
    auto message=impl_->channel.receive(deadline);
    if(!message || !impl_->session->accept(*message) || !canCommitExit()){impl_->abort();return CoordinationResult::Failed;}
    return CoordinationResult::WaitingForAppExit;
}
bool Coordinator::canCommitExit() const{
    return impl_->session && !impl_->failed && impl_->session->canExit() && impl_->process.alive()
        && impl_->process.matches(impl_->process.stamp());
}
bool Coordinator::commitExit(Deadline deadline){
    if(!canCommitExit() || !impl_->session->accept(impl_->message(MessageKind::CommitExit))
        || !impl_->channel.send(impl_->message(MessageKind::CommitExit),deadline)){impl_->abort();return false;}return true;
}
CoordinationResult Coordinator::proceedIfExited(Deadline deadline){
    // Fail-closed first: after an abort the Proceed latch must never surface
    // ProceedSent again, so the failed check precedes the latch.
    if(!impl_->session || impl_->failed){impl_->abort();return CoordinationResult::Failed;}
    if(impl_->proceedSent)return CoordinationResult::ProceedSent;
    if(impl_->session->state()!=HandoffState::ExitCommitted
        || !impl_->applicationHeld){impl_->abort();return CoordinationResult::Failed;}
    // The gate observes the real application process handle, never a message:
    // only its signalled state proves the exit actually happened.
    const auto waited=WaitForSingleObject(impl_->application.handle(),0);
    if(waited==WAIT_TIMEOUT)return CoordinationResult::PeerRunning;
    if(waited!=WAIT_OBJECT_0 || !impl_->session->accept(impl_->message(MessageKind::Proceed))
        || !impl_->channel.send(impl_->message(MessageKind::Proceed),deadline)){impl_->abort();return CoordinationResult::Failed;}
    impl_->proceedSent=true;return CoordinationResult::ProceedSent;
}
CoordinationResult Coordinator::finish(Deadline deadline){
    if(!impl_->session || impl_->failed)return CoordinationResult::Failed;
    if(!impl_->completeReceived){
        auto message=impl_->channel.receive(deadline);
        if(!message || !impl_->session->accept(*message) || impl_->session->state()!=HandoffState::Complete){impl_->abort();return CoordinationResult::Failed;}
        impl_->completeReceived=true;
    }
    if(WaitForSingleObject(impl_->process.handle(),remaining(deadline))!=WAIT_OBJECT_0)return CoordinationResult::PeerRunning;
    DWORD code=0;if(!GetExitCodeProcess(impl_->process.handle(),&code) || code!=0){impl_->abort();return CoordinationResult::Failed;}
    if(impl_->elevated)return CoordinationResult::ManualRestartRequired;
    impl_->finishedOk=true;
    return CoordinationResult::Complete;
}
CoordinationResult Coordinator::restart(Deadline deadline){
    // Single-shot: a second call can never launch another GUI instance.
    if(!impl_->installerChain || !impl_->finishedOk || impl_->failed || impl_->elevated
        || impl_->restarted.handle())return CoordinationResult::Failed;
    const auto fail=[]{return CoordinationResult::RestartFailed;};
    // Registration recheck: the directory must still be the pinned registered
    // installation with a consistent marker and manifest, and the new main
    // executable must be present inside it.
    DirectoryIdentity current{};
    if(!InstallationActivity::probeIdentity(impl_->installRoot,current)
        || current.volumeSerial!=impl_->installIdentity.volumeSerial || current.fileId!=impl_->installIdentity.fileId)
        return fail();
    const auto image=impl_->installRoot+L"\\ZzLogg.exe";
    if(!filePrefix(impl_->installRoot+L"\\.zzlogg-install-root","ZzLogg ")
        || !filePrefix(impl_->installRoot+L"\\.zzlogg-files.manifest","ZZTXMAN1")
        || !filePresent(image))return fail();
    // Ordinary-user relaunch: the coordinator never elevates the GUI, and
    // only the bounded --data-dir value rides the command line.
    std::wstring command=L"\""+image+L"\"";
    if(!impl_->dataDirectory.empty())command+=L" --data-dir \""+impl_->dataDirectory+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION launched{};
    if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,impl_->installRoot.c_str(),&startup,&launched))
        return fail();
    Handle thread(launched.hThread),process(launched.hProcess);
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process.get(),&created,&exited,&kernel,&user))return fail();
    ProcessIdentity restarted;
    if(!restarted.adopt(process.release(),{launched.dwProcessId,(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime}))
        return fail();
    // The coordinator owns the launched candidate until its own destruction,
    // whatever the confirmation outcome; it is never terminated by name.
    impl_->restarted=std::move(restarted);
    // Bounded startup confirmation: process alive AND the directory-scoped
    // single-instance endpoint present. Installation completion is never
    // treated as startup health.
    const auto endpoint=singleInstancePipeName(impl_->installIdentity,L"ZzLogg.exe");
    if(endpoint.empty())return fail();
    while(remaining(deadline)){
        if(!impl_->restarted.alive())return fail();
        if(WaitNamedPipeW(endpoint.c_str(),20)!=FALSE && impl_->restarted.alive())
            return CoordinationResult::Restarted;
        // A missing endpoint fails WaitNamedPipeW instantly; never busy-poll.
        Sleep(20);
    }
    return fail();
}
void Coordinator::cancel(Deadline deadline){
    if(impl_->session && !impl_->failed){impl_->channel.send(impl_->message(MessageKind::Cancel),deadline);impl_->session->accept(impl_->message(MessageKind::Cancel));}
    impl_->abort();
}
const ProcessIdentity& Coordinator::process() const{return impl_->process;}
const std::wstring& Coordinator::runtimePath() const{return impl_->copy.path();}
}
