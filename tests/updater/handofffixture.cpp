#include "bootstrap_win_p.h"
#include "installationactivity_win.h"
#include "txengine_win.h"
#include "updatertesthelpers.h"
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
TxEngineOptions fixtureOptions(TxRegistry& registry) {
    TxEngineOptions options;
    const auto user=currentUserSid();
    options.journal.writers={user}; options.journal.readers={user};
    options.registry=&registry;
    // Fixture stand-in for the production protected-image assertion: the
    // coordinator runtime-copies this fixture outside any protected root, so
    // the accept path is injected here. The production check (prefix + parent
    // ACL) and its failure paths are tested in-process against real ACLs.
    options.protectedImage=[](const std::wstring&,const std::wstring&){return true;};
    return options;
}
// Full transaction protocol body after a validated Ready: the real TxEngine
// driven as a protocol client with injected registry/protected-root seams,
// exactly the production sequence recheck -> AwaitingAppExit -> CommitExit ->
// Proceed -> transaction -> Complete/Failed.
int runEngineProtocol(const std::wstring& mode,LocalChannel& channel,Message message,const TxEngineRequest& request) {
    FileRegistry registry(envVar(L"ZZLOGG_TX_FAKEREG"));
    TxEngine engine(request,fixtureOptions(registry));
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
// Engine stand-in on the finalized argv contract: credentials arrive only
// through the current-user-private credential file named by --txid, are
// validated and deleted, then the protocol runs against the live coordinator.
int runCredentialEngine(int argc,wchar_t** argv) {
    std::wstring install,staging,txroot,txid,version;
    bool seenInstall=false,seenStaging=false,seenTxroot=false,seenTxid=false,seenVersion=false;
    for(int i=2;i<argc;++i) {
        const std::wstring flag=argv[i];
        std::wstring* value=nullptr;bool* seen=nullptr;
        if(flag==L"--install"){value=&install;seen=&seenInstall;}
        else if(flag==L"--staging"){value=&staging;seen=&seenStaging;}
        else if(flag==L"--txroot"){value=&txroot;seen=&seenTxroot;}
        else if(flag==L"--txid"){value=&txid;seen=&seenTxid;}
        else if(flag==L"--version"){value=&version;seen=&seenVersion;}
        else return UsageRejected;
        if(*seen || i+1>=argc) return UsageRejected;
        *value=argv[++i];*seen=true;
    }
    if(!seenInstall || !seenStaging || !seenTxroot || !seenTxid || !seenVersion) return UsageRejected;
    const auto txidValue=parseHexId(txid);
    if(!txidValue) return UsageRejected;
    CredentialData credential;
    if(!readCredentialFile(txid,credential)) return BootstrapRejected;
    if(!deleteCredentialFile(txid)) return BootstrapRejected;
    record(L"engine txid="+txid);
    ProcessIdentity server;
    const ProcessStamp serverStamp{static_cast<DWORD>(credential.coordinatorPid),credential.coordinatorCreated};
    if(!server.open(serverStamp.pid) || !server.matches(serverStamp)) return BootstrapRejected;
    LocalChannel channel;
    if(!channel.connect(credential.transaction,server,after(3000))) return 52;
    Message message{MessageKind::Hello,credential.transaction,credential.token};
    if(!channel.send(message,after(1000))) return 53;
    const auto ready=channel.receive(after(1500));
    if(!ready || ready->kind!=MessageKind::Ready) return 0;
    recordKind(*ready);
    const auto behavior=envVar(L"ZZLOGG_FIXTURE_ENGINE");
    if(behavior==L"prepare-fail") {
        message.kind=MessageKind::Failed;channel.send(message,after(1000));return 42;
    }
    const auto mode=envVar(L"ZZLOGG_HANDOFF_FIXTURE");
    if(mode==L"engine" || mode==L"engine-violation")
        return runEngineProtocol(mode,channel,message,{install,staging,txroot,txidValue,version});
    // Protocol-only stand-in: no payload transaction work.
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000))) return 54;
    const auto commit=channel.receive(after(5000));
    if(!commit || commit->kind!=MessageKind::CommitExit) return 56;
    recordKind(*commit);
    const auto proceed=channel.receive(after(15000));
    if(proceed) recordKind(*proceed);
    if(!proceed || proceed->kind!=MessageKind::Proceed) return 56;
    message.kind=MessageKind::Complete;
    channel.send(message,after(1000));
    return 0;
}
// NSIS restricted-entry stand-in: validates the locator shape, then runs the
// engine with the finalized argv contract and propagates its exit code
// (ExecWait model). What NSIS would know independently (target, staging, tx
// root, payload version) arrives through the fixture environment.
int runInstallerFixture(const std::wstring& switchArg) {
    record(L"installer switch="+switchArg);
    if(switchArg.rfind(L"/ZzLoggUpgrade=",0)!=0) return UsageRejected;
    const auto locator=switchArg.substr(15);
    if(!parseHexId(locator)) return UsageRejected;
    wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
    if(!length || length>=32768) return UsageRejected;
    std::wstring command=L"\""+std::wstring(self,length)+L"\" --tx-engine"
        +L" --install \""+envVar(L"ZZLOGG_TX_INSTALL")+L"\""
        +L" --staging \""+envVar(L"ZZLOGG_TX_STAGING")+L"\""
        +L" --txroot \""+envVar(L"ZZLOGG_TX_TXROOT")+L"\""
        +L" --txid "+locator
        +L" --version \""+envVar(L"ZZLOGG_TX_VERSION")+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return UsageRejected;
    Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),120000)!=WAIT_OBJECT_0){TerminateProcess(child.get(),90);return 90;}
    DWORD code=90;GetExitCodeProcess(child.get(),&code);
    return static_cast<int>(code);
}
// Restarted-GUI stand-in (fixture copied under the production executable
// name): records the received --data-dir, then creates the directory-scoped
// single-instance endpoint exactly where KDSingleApplication would listen,
// unless the behavior variable asks for an early death or a missing endpoint.
int runGuiFixture(int argc,wchar_t** argv) {
    std::wstring dataDir;
    for(int i=1;i+1<argc;++i) if(std::wstring(argv[i])==L"--data-dir"){dataDir=argv[i+1];break;}
    record(L"gui pid="+std::to_wstring(GetCurrentProcessId())+L" datadir="+dataDir);
    const auto behavior=envVar(L"ZZLOGG_FIXTURE_GUI");
    if(behavior==L"die") return 7;
    if(behavior==L"noendpoint"){Sleep(8000);return 0;}
    wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
    if(!length || length>=32768) return 8;
    const fs::path image(std::wstring(self,length));
    DirectoryIdentity identity{};
    if(!InstallationActivity::probeIdentity(image.parent_path().wstring(),identity)) return 8;
    const auto name=singleInstancePipeName(identity,image.filename().wstring());
    if(name.empty()) return 8;
    Handle pipe(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,64,64,0,nullptr));
    if(!pipe) return 8;
    Sleep(4000);
    return 0;
}
// Authorized recovery stand-in: no handshake, target recheck plus strict
// journal-driven reverse replay only. Models the NSIS restricted entry.
int runEngineRecovery() {
    FileRegistry registry(envVar(L"ZZLOGG_TX_FAKEREG"));
    auto options=fixtureOptions(registry);
    const auto result=TxEngine::recover(envVar(L"ZZLOGG_TX_TXROOT"),parseHexId(envVar(L"ZZLOGG_TX_TXID")),
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
    // 3C installer chain: the fixture plays the NSIS restricted entry, the
    // credential-bootstrapped engine, or (renamed to the production
    // executable name) the restarted GUI.
    if(argc==2 && std::wstring(argv[1]).rfind(L"/ZzLoggUpgrade=",0)==0)return runInstallerFixture(argv[1]);
    if(argc>=2 && std::wstring(argv[1])==L"--tx-engine")return runCredentialEngine(argc,argv);
    {
        wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
        if(length && length<32768 && fs::path(std::wstring(self,length)).filename()==L"ZzLogg.exe")
            return runGuiFixture(argc,argv);
    }
    ChildBootstrap bootstrap;if(!bootstrap.open(argc,argv))return 51;
    record(L"datadir="+std::wstring(bootstrap.data().dataDirectory));
    wchar_t modeText[80]{};GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",modeText,80);std::wstring mode=modeText;
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
