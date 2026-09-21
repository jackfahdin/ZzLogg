// ZzLoggUpdateTx.exe: the payload-embedded transaction engine. Static CRT,
// Qt-free, runs only from the protected staging extracted by the Inno Setup
// restricted upgrade entry (task 4 wires the packaging). The restricted argv
// contract (3C task 5): exactly the flag/value pairs --install --staging
// --txroot --txid --version; anything else is a usage rejection. Transaction
// credentials never travel on the command line: the engine locates the
// current-user-private credential file by the --txid locator, validates
// ownership/shape/coordinator identity, and deletes it after reading. As a
// handoff protocol client it performs Hello -> target recheck ->
// AwaitingAppExit -> CommitExit -> Proceed -> transaction -> Complete/Failed;
// any file modification before Proceed is a contract violation, so prepare()
// is strictly read-only and the journal is opened only after the gate. The
// --recover mode is the authorized recovery path: it rechecks the target and
// replays the journal strictly in reverse, never accepting an external file
// list.
#include "bootstrap_win_p.h"
#include "txcontract_win_p.h"
#include "txengine_win.h"
namespace {
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
std::wstring argument(int argc,wchar_t** argv,const wchar_t* name) {
    for(int i=1;i+1<argc;++i) if(std::wstring(argv[i])==name) return argv[i+1];
    return {};
}
struct EngineArguments { std::wstring install,staging,txroot,txid,version; };
// Strict flag/value scan: every token must be a known flag with a fresh
// value; all five flags are required exactly once. No positional arguments,
// no duplicates, no unknown switches.
bool parseArguments(int argc,wchar_t** argv,EngineArguments& out) {
    bool seenInstall=false,seenStaging=false,seenTxroot=false,seenTxid=false,seenVersion=false;
    for(int i=1;i<argc;++i) {
        const std::wstring flag=argv[i];
        std::wstring* value=nullptr;bool* seen=nullptr;
        if(flag==L"--install"){value=&out.install;seen=&seenInstall;}
        else if(flag==L"--staging"){value=&out.staging;seen=&seenStaging;}
        else if(flag==L"--txroot"){value=&out.txroot;seen=&seenTxroot;}
        else if(flag==L"--txid"){value=&out.txid;seen=&seenTxid;}
        else if(flag==L"--version"){value=&out.version;seen=&seenVersion;}
        else return false;
        if(*seen || i+1>=argc) return false;
        *value=argv[++i];*seen=true;
    }
    return seenInstall && seenStaging && seenTxroot && seenTxid && seenVersion
        && !out.install.empty() && !out.staging.empty() && !out.txroot.empty() && !out.version.empty();
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
    // Authorized recovery: invoked only by the Inno Setup restricted entry after
    // explicit UAC authorization. No handshake; the mode performs the target
    // recheck and nothing beyond it.
    if(argc>=2 && std::wstring(argv[1])==L"--recover") return runRecovery(argc,argv);
    EngineArguments arguments;std::uint64_t txid=0;
    if(!parseArguments(argc,argv,arguments) || !parseTxid(arguments.txid,txid)) return UsageRejected;
    // The credential file replaces the retired inherited-mapping bootstrap:
    // it is the only channel for the transaction id, the one-time token and
    // the coordinator identity. Missing, foreign-owned or identity-mismatched
    // files reject before any handshake; a validated file is deleted at once.
    CredentialData credential;
    if(!readCredentialFile(arguments.txid,credential) || !deleteCredentialFile(arguments.txid)) return BootstrapRejected;
    ProcessIdentity coordinator;
    const ProcessStamp coordinatorStamp{static_cast<DWORD>(credential.coordinatorPid),credential.coordinatorCreated};
    if(!coordinator.open(coordinatorStamp.pid) || !coordinator.matches(coordinatorStamp)) return BootstrapRejected;
    LocalChannel channel; const auto deadline=after(3000);
    if(!channel.connect(credential.transaction,coordinator,deadline)) return BootstrapRejected;
    Message message{MessageKind::Hello,credential.transaction,credential.token};
    if(!channel.send(message,deadline)) return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction
        || ready->token!=message.token || !coordinator.alive()) return BootstrapRejected;
    TxEngine engine({arguments.install,arguments.staging,arguments.txroot,txid,arguments.version},productionOptions());
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
