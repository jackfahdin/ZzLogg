// ZzLoggUpdateTx.exe: the payload-embedded transaction engine. Static CRT,
// Qt-free, runs only from the protected staging extracted by the NSIS
// restricted upgrade entry (task 4 wires the packaging). As a handoff protocol
// client it performs Hello -> target recheck -> AwaitingAppExit -> CommitExit
// -> Proceed -> transaction -> Complete/Failed; any file modification before
// Proceed is a contract violation, so prepare() is strictly read-only and the
// journal is opened only after the gate. The --recover mode is the authorized
// recovery path: it rechecks the target and replays the journal strictly in
// reverse, never accepting an external file list.
#include "bootstrap_win_p.h"
#include "txengine_win.h"
namespace {
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
constexpr int UsageRejected=2;
constexpr int ProtocolViolation=47;
int exitForOutcome(TxOutcome outcome) {
    switch(outcome) {
    case TxOutcome::Applied: case TxOutcome::Recovered: case TxOutcome::NothingToRecover: return 0;
    case TxOutcome::Rejected: return 42;
    case TxOutcome::Conflict: return 43;
    case TxOutcome::RolledBack: return 44;
    case TxOutcome::NeedsAuthorizedRecovery: return 45;
    case TxOutcome::RecoveryFailed: return 46;
    default: return ProtocolViolation;
    }
}
std::wstring argument(int argc,wchar_t** argv,const wchar_t* name) {
    for(int i=1;i+1<argc;++i) if(std::wstring(argv[i])==name) return argv[i+1];
    return {};
}
bool parseTxid(const std::wstring& text,std::uint64_t& value) {
    value=0;
    if(text.size()!=16) return false;
    for(const auto c:text) {
        value<<=4;
        if(c>=L'0' && c<=L'9') value|=c-L'0';
        else if(c>=L'a' && c<=L'f') value|=c-L'a'+10;
        else return false;
    }
    return value!=0;
}
TxEngineOptions productionOptions() {
    TxEngineOptions options; options.journal=TxJournalOptions::production(); return options;
}
int runRecovery(int argc,wchar_t** argv) {
    const auto install=argument(argc,argv,L"--install");
    const auto txroot=argument(argc,argv,L"--txroot");
    std::uint64_t txid=0;
    if(install.empty() || txroot.empty() || !parseTxid(argument(argc,argv,L"--txid"),txid)) return UsageRejected;
    const auto result=TxEngine::recover(txroot,txid,install,productionOptions());
    return exitForOutcome(result.outcome);
}
}
int wmain(int argc,wchar_t** argv) {
    // Authorized recovery: invoked only by the NSIS restricted entry after
    // explicit UAC authorization. No handshake; the mode performs the target
    // recheck and nothing beyond it.
    if(argc>=2 && std::wstring(argv[1])==L"--recover") return runRecovery(argc,argv);
    if(argc<2) return BootstrapRejected;
    const auto install=argument(argc,argv,L"--install");
    const auto staging=argument(argc,argv,L"--staging");
    const auto txroot=argument(argc,argv,L"--txroot");
    const auto version=argument(argc,argv,L"--version");
    std::uint64_t txid=0;
    if(install.empty() || staging.empty() || txroot.empty() || version.empty()
        || !parseTxid(argument(argc,argv,L"--txid"),txid)) return UsageRejected;
    // The bootstrap consumes exactly the mapping handle; request parameters are
    // NSIS-side hints that prepare() independently rechecks.
    wchar_t* bootstrapArgv[2]={argv[0],argv[1]};
    ChildBootstrap bootstrap; if(!bootstrap.open(2,bootstrapArgv)) return BootstrapRejected;
    LocalChannel channel; const auto deadline=after(3000);
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),deadline)) return BootstrapRejected;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,deadline)) return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction
        || ready->token!=message.token || !bootstrap.parent().alive()) return BootstrapRejected;
    TxEngine engine({install,staging,txroot,txid,version},productionOptions());
    const auto prepared=engine.prepare();
    if(prepared.outcome!=TxOutcome::Prepared) {
        message.kind=MessageKind::Failed; channel.send(message,deadline);
        return exitForOutcome(prepared.outcome);
    }
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,deadline)) return ProtocolViolation;
    // Gate: CommitExit must arrive first, then Proceed. Nothing else opens the
    // transaction, and execute() performs the first modification.
    const auto commit=channel.receive(after(60000));
    if(!commit || commit->kind!=MessageKind::CommitExit) {
        message.kind=MessageKind::Failed; channel.send(message,deadline); return ProtocolViolation;
    }
    const auto proceed=channel.receive(after(60000));
    if(!proceed || proceed->kind!=MessageKind::Proceed) {
        message.kind=MessageKind::Failed; channel.send(message,deadline); return ProtocolViolation;
    }
    const auto done=engine.execute();
    message.kind=done.outcome==TxOutcome::Applied?MessageKind::Complete:MessageKind::Failed;
    channel.send(message,deadline);
    return exitForOutcome(done.outcome);
}
