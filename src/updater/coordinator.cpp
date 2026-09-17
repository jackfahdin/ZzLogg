#include "coordinator_p.h"
#include <thread>
namespace zzlogg::updater::detail {
struct Coordinator::Impl {
    RuntimeCopy copy;ProcessIdentity process;LocalChannel channel;
    TransactionId transaction{};SessionToken token{};std::unique_ptr<HandoffSession> session;
    ProcessIdentity application;
    bool elevated=false,failed=false,completeReceived=false,applicationHeld=false,proceedSent=false;
    void abort(){failed=true;channel.close();}
    Message message(MessageKind kind) const{return {kind,transaction,token};}
};
Coordinator::Coordinator():impl_(std::make_unique<Impl>()){}
Coordinator::~Coordinator(){
    impl_->channel.close();
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
    if(!impl_->copy.create(source,base) || !impl_->channel.create(impl_->transaction)
        || !launchCopy(impl_->copy,impl_->transaction,impl_->token,reservedIdentity,dataDirectory,impl_->process)){impl_->abort();return false;}return true;
}
bool Coordinator::authenticate(Deadline deadline){
    if(!impl_->session || impl_->failed || impl_->session->state()!=HandoffState::Connecting
        || !impl_->channel.accept(impl_->process,deadline)){impl_->abort();return false;}
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
    if(impl_->proceedSent)return CoordinationResult::ProceedSent;
    if(!impl_->session || impl_->failed || impl_->session->state()!=HandoffState::ExitCommitted
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
    return impl_->elevated?CoordinationResult::ManualRestartRequired:CoordinationResult::Complete;
}
void Coordinator::cancel(Deadline deadline){
    if(impl_->session && !impl_->failed){impl_->channel.send(impl_->message(MessageKind::Cancel),deadline);impl_->session->accept(impl_->message(MessageKind::Cancel));}
    impl_->abort();
}
const ProcessIdentity& Coordinator::process() const{return impl_->process;}
const std::wstring& Coordinator::runtimePath() const{return impl_->copy.path();}
}
