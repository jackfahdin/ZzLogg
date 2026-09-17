#include "bootstrap_win_p.h"
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
int wmain(int argc,wchar_t** argv){
    ChildBootstrap bootstrap;if(!bootstrap.open(argc,argv))return BootstrapRejected;
    LocalChannel channel;const auto deadline=after(3000);
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),deadline))return BootstrapRejected;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,deadline))return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction || ready->token!=message.token
        || !bootstrap.parent().alive())return BootstrapRejected;
    // Publisher policy is not configured; no installer launch path exists in
    // this stage. The 3C installer-chain composition (credential file,
    // restricted ShellExecuteEx runas launcher, engine channel server and the
    // ordinary-user restart with startup confirmation) lives in the
    // Coordinator and is exercised by dedicated test targets; it is composed
    // here only when production signing and release identity open the gate.
    message.kind=MessageKind::Failed;channel.send(message,deadline);return ExecutionDisabled;
}
