#include "bootstrap_win_p.h"
#include "txengine_win.h"
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
namespace {
// Ordered message/data-directory observation log, consumed by the harness to
// assert the CommitExit -> Proceed -> Complete sequence. Fixture-only seam.
void record(const std::wstring& line) {
    wchar_t path[512]{};
    if(!GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",path,512) || !path[0])return;
    std::wofstream out(path,std::ios::app);out<<line<<L'\n';
}
void recordKind(const Message& message) { record(L"kind="+std::to_wstring(static_cast<unsigned>(message.kind))); }
std::wstring envVar(const wchar_t* name) {
    wchar_t buffer[32768]{};
    const auto length=GetEnvironmentVariableW(name,buffer,32768);
    return length && length<32768?std::wstring(buffer,buffer+length):std::wstring();
}
std::wstring fixtureUserSid() {
    HANDLE rawToken=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken)) return {};
    Handle token(rawToken);
    DWORD size=0; GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);
    if(!size || size>4096) return {};
    std::vector<BYTE> bytes(size);
    if(!GetTokenInformation(token.get(),TokenUser,bytes.data(),size,&size)) return {};
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)) return {};
    std::wstring result(sid); LocalFree(sid); return result;
}
std::uint64_t parseHex(const std::wstring& text) {
    std::uint64_t value=0;
    if(text.size()!=16) return 0;
    for(const auto c:text) {
        value<<=4;
        if(c>=L'0' && c<=L'9') value|=c-L'0';
        else if(c>=L'a' && c<=L'f') value|=c-L'a'+10;
        else return 0;
    }
    return value;
}
// File-backed fake registry, one line per value: name \t S|D \t value. The
// engine's key whitelist is exercised for real: any key other than the exact
// uninstall key is refused here as well. The post-write sleep gives the
// harness a deterministic kill window inside an applied registry write.
class FileRegistry:public TxRegistry {
public:
    explicit FileRegistry(std::wstring path):path_(std::move(path)) {
        std::wifstream in(path_); std::wstring line;
        while(std::getline(in,line)) {
            const auto first=line.find(L'\t'); if(first==std::wstring::npos) continue;
            const auto second=line.find(L'\t',first+1); if(second==std::wstring::npos) continue;
            const auto name=line.substr(0,first),type=line.substr(first+1,second-first-1),value=line.substr(second+1);
            if(type==L"S") strings_[name]=value;
            else if(type==L"D") dwords_[name]=std::wcstoul(value.c_str(),nullptr,10);
        }
    }
    bool readString(const std::wstring& key,const std::wstring& name,std::wstring& value) override {
        if(!allowedRegistryKey(key)) return false;
        const auto it=strings_.find(name); if(it==strings_.end()) return false; value=it->second; return true;
    }
    bool readDword(const std::wstring& key,const std::wstring& name,std::uint32_t& value) override {
        if(!allowedRegistryKey(key)) return false;
        const auto it=dwords_.find(name); if(it==dwords_.end()) return false; value=it->second; return true;
    }
    bool writeString(const std::wstring& key,const std::wstring& name,const std::wstring& value) override {
        if(!allowedRegistryKey(key)) return false;
        strings_[name]=value;
        std::wofstream out(path_,std::ios::trunc);
        for(const auto& [n,v]:strings_) out<<n<<L"\tS\t"<<v<<L'\n';
        for(const auto& [n,v]:dwords_) out<<n<<L"\tD\t"<<v<<L'\n';
        out.flush();
        Sleep(1200);
        return true;
    }
private:
    std::wstring path_;
    std::map<std::wstring,std::wstring> strings_;
    std::map<std::wstring,std::uint32_t> dwords_;
};
int exitForOutcome(TxOutcome outcome) {
    switch(outcome) {
    case TxOutcome::Applied: case TxOutcome::Recovered: case TxOutcome::NothingToRecover: return 0;
    case TxOutcome::Rejected: return 42;
    case TxOutcome::Conflict: return 43;
    case TxOutcome::RolledBack: return 44;
    case TxOutcome::NeedsAuthorizedRecovery: return 45;
    case TxOutcome::RecoveryFailed: return 46;
    default: return 47;
    }
}
TxEngineOptions fixtureOptions(TxRegistry& registry) {
    TxEngineOptions options;
    const auto user=fixtureUserSid();
    options.journal.writers={user}; options.journal.readers={user};
    options.registry=&registry;
    // Fixture stand-in for the production protected-image assertion: the
    // coordinator runtime-copies this fixture outside any protected root, so
    // the accept path is injected here. The production check (prefix + parent
    // ACL) and its failure paths are tested in-process against real ACLs.
    options.protectedImage=[](const std::wstring&,const std::wstring&){return true;};
    return options;
}
TxEngineRequest fixtureRequest() {
    TxEngineRequest request;
    request.installRoot=envVar(L"ZZLOGG_TX_INSTALL");
    request.stagingDir=envVar(L"ZZLOGG_TX_STAGING");
    request.journalRoot=envVar(L"ZZLOGG_TX_TXROOT");
    request.txid=parseHex(envVar(L"ZZLOGG_TX_TXID"));
    request.displayVersion=envVar(L"ZZLOGG_TX_VERSION");
    return request;
}
// Engine stand-in: the real TxEngine library driven as a protocol client with
// injected registry/protected-root seams, exactly the production sequence
// Hello -> recheck -> AwaitingAppExit -> CommitExit -> Proceed -> transaction
// -> Complete/Failed.
int runEngineFixture(const std::wstring& mode,ChildBootstrap& bootstrap) {
    LocalChannel channel;
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),after(3000)))return 52;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,after(1000)))return 53;
    const auto ready=channel.receive(after(1500));
    if(!ready || ready->kind!=MessageKind::Ready)return 0;
    recordKind(*ready);
    FileRegistry registry(envVar(L"ZZLOGG_TX_FAKEREG"));
    TxEngine engine(fixtureRequest(),fixtureOptions(registry));
    const auto prepared=engine.prepare();
    record(L"engine prepare="+std::to_wstring(static_cast<int>(prepared.outcome)));
    if(prepared.outcome!=TxOutcome::Prepared) {
        message.kind=MessageKind::Failed;channel.send(message,after(1000));
        return exitForOutcome(prepared.outcome);
    }
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000)))return 54;
    const auto commit=channel.receive(after(5000));
    if(!commit || commit->kind!=MessageKind::CommitExit)return 56;
    recordKind(*commit);
    if(mode==L"engine-violation") {
        // Contract violation: modify the installation and report Complete
        // without waiting for the Proceed gate; the coordinator must reject.
        {std::ofstream out(fs::path(envVar(L"ZZLOGG_TX_INSTALL"))/L"violation-before-proceed.txt");out<<"written before Proceed";}
        record(L"write");
        message.kind=MessageKind::Complete;channel.send(message,after(1000));return 0;
    }
    const auto proceed=channel.receive(after(15000));
    if(proceed)recordKind(*proceed);
    if(!proceed || proceed->kind!=MessageKind::Proceed)return 56;
    const auto done=engine.execute();
    record(L"engine outcome="+std::to_wstring(static_cast<int>(done.outcome)));
    message.kind=done.outcome==TxOutcome::Applied?MessageKind::Complete:MessageKind::Failed;
    channel.send(message,after(1000));
    return exitForOutcome(done.outcome);
}
// Authorized recovery stand-in: no handshake, target recheck plus strict
// journal-driven reverse replay only. Models the NSIS restricted entry.
int runEngineRecovery() {
    FileRegistry registry(envVar(L"ZZLOGG_TX_FAKEREG"));
    auto options=fixtureOptions(registry);
    const auto result=TxEngine::recover(envVar(L"ZZLOGG_TX_TXROOT"),parseHex(envVar(L"ZZLOGG_TX_TXID")),
        envVar(L"ZZLOGG_TX_INSTALL"),std::move(options));
    record(L"engine recover="+std::to_wstring(static_cast<int>(result.outcome)));
    return exitForOutcome(result.outcome);
}
}
// All alternative behavior belongs to this non-deployed test target only.
int wmain(int argc,wchar_t** argv) {
    // Application stand-in for the coordinator Proceed gate: a plain bounded
    // sleeper with no bootstrap and no channel.
    if(argc==3 && std::wstring(argv[1])==L"--app"){Sleep(static_cast<DWORD>(std::wcstoul(argv[2],nullptr,10)));return 0;}
    if(argc==2 && std::wstring(argv[1])==L"--engine-recover")return runEngineRecovery();
    ChildBootstrap bootstrap;if(!bootstrap.open(argc,argv))return 51;
    record(L"datadir="+std::wstring(bootstrap.data().dataDirectory));
    wchar_t modeText[80]{};GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",modeText,80);std::wstring mode=modeText;
    if(mode==L"engine" || mode==L"engine-violation")return runEngineFixture(mode,bootstrap);
    if(mode==L"early")return 0;
    if(mode==L"timeout"){Sleep(500);return 0;}
    LocalChannel channel;
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),after(3000)))return 52;
    auto message=Message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(mode==L"token")message.token[0]^=1;
    if(mode==L"order")message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000)))return 53;
    auto ready=channel.receive(after(1500));if(!ready || ready->kind!=MessageKind::Ready)return 0;
    recordKind(*ready);
    if(mode==L"hello-only"){Sleep(400);return 0;}
    if(mode==L"exit-after-hello")return 0;
    message.kind=mode==L"replay"?MessageKind::Hello:mode==L"cancel"?MessageKind::Cancel:MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000)))return 54;
    if(mode==L"cancel" || mode==L"replay")return 0;
    if(mode==L"die-waiting")return 0; // Peer dies after AwaitingAppExit, before CommitExit.
    auto commit=channel.receive(after(2000));
    if(mode==L"orphan"){
        // Keep the child alive after the real parent dies so the harness can
        // test self-owned leases after all inherited parent handles vanished.
        if(WaitForSingleObject(bootstrap.parent().handle(),1000)!=WAIT_OBJECT_0)return 55;
        Sleep(600);return 0;
    }
    if(!commit || commit->kind!=MessageKind::CommitExit)return 0;
    recordKind(*commit);
    if(mode==L"early-complete"){
        // Contract violation: pretend to modify the installation and report
        // Complete without waiting for the Proceed gate.
        record(L"write");
        message.kind=MessageKind::Complete;channel.send(message,after(1000));return 0;
    }
    // The gate budget must outlast the harness's stand-in lifetime; a cancelled
    // or broken channel still ends this wait immediately.
    auto proceed=channel.receive(after(mode==L"waitgate"?1000:10000));
    if(proceed)recordKind(*proceed);
    if(mode==L"waitgate"){
        // Hold the bootstrap directory observer until the harness opens the
        // named gate, so gate continuity is observable after the parent's
        // real exit. A Proceed that arrived first is answered with Complete; its absence
        // (harness cancelled instead) never blocks the gate wait.
        if(proceed && proceed->kind==MessageKind::Proceed){message.kind=MessageKind::Complete;channel.send(message,after(1000));}
        wchar_t name[256]{};GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_GATE",name,256);
        Handle gate(OpenEventW(SYNCHRONIZE,FALSE,name));
        if(!gate)return 9;
        WaitForSingleObject(gate.get(),30000);
        return 0;
    }
    if(!proceed || proceed->kind!=MessageKind::Proceed)return 56;
    message.kind=MessageKind::Complete;channel.send(message,after(1000));
    if(mode==L"linger")Sleep(600);
    return 0;
}
