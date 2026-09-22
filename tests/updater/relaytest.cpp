#include "bootstrap_win_p.h"
#include "updatertesthelpers.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
namespace {
// The stand-in application stays alive this long after committing its exit:
// the Proceed gate must stay shut for the whole window.
constexpr DWORD ApplicationLingerMs=1500;
constexpr DWORD ObservationPollMs=25;

// GUI -> relay bootstrap request. Empty installer-chain fields degrade to a
// plain protocol handshake, exactly as the mapping parser reads them.
struct RelayRequest {
    std::wstring installerPath,installRoot,dataDirectory;
    uint64_t packageSize=0;
    std::array<uint8_t,32> packageSha256{};
};
// What the harness could observe about the relay's ordering from outside.
struct RelayObservation { bool awaitingBeforeCommit=false,proceedAfterApplicationExit=false; };

std::vector<std::wstring> readLines(const fs::path& path) {
    std::vector<std::wstring> lines;std::wifstream in(path);std::wstring line;
    while(std::getline(in,line))lines.push_back(line);
    return lines;
}
bool hasLine(const std::vector<std::wstring>& lines,const wchar_t* text) {
    return std::find(lines.begin(),lines.end(),std::wstring(text))!=lines.end();
}
// Readers poll these while the writer is still running, so every file is
// published by rename, never observed half written.
void publish(const fs::path& path,const std::vector<std::wstring>& lines) {
    const auto pending=fs::path(path).concat(L".pending");
    {std::wofstream out(pending,std::ios::trunc);for(const auto& line:lines)out<<line<<L'\n';}
    std::error_code ec;fs::rename(pending,path,ec);
}
std::array<uint8_t,32> fileSha256(const fs::path& path) {
    std::array<uint8_t,32> digest{};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return digest;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) {
        std::ifstream file(path,std::ios::binary);
        std::array<char,64*1024> buffer{};
        for(;;) {
            file.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
            const auto read=static_cast<ULONG>(file.gcount());
            if(read && BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),read,0)<0){ok=false;break;}
            if(!file)break;
        }
        ok=ok && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    }
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok)digest.fill(0);
    return digest;
}
std::wstring digestText(const std::array<uint8_t,32>& digest) {
    std::wstring text;constexpr wchar_t hex[]=L"0123456789abcdef";
    for(const auto byte:digest){text+=hex[byte>>4];text+=hex[byte&15];}
    return text;
}
void copyBounded(wchar_t (&field)[PathCapacity],const std::wstring& value) {
    if(value.size()>=PathCapacity)throw std::runtime_error("relay test path exceeds the bootstrap capacity");
    std::copy(value.begin(),value.end(),field);
}
BootstrapData makeBootstrap(const RelayRequest& request) {
    BootstrapData data{};
    if(!randomBytes(data.transaction.data(),16) || !randomBytes(data.token.data(),32))
        throw std::runtime_error("cannot generate relay transaction material");
    copyBounded(data.dataDirectory,request.dataDirectory);
    copyBounded(data.installerPath,request.installerPath);
    copyBounded(data.installRoot,request.installRoot);
    data.packageSize=request.packageSize;
    std::copy(request.packageSha256.begin(),request.packageSha256.end(),data.packageSha256);
    return data;
}
struct RelayChild {
    Handle inheritedParent,inheritedMapping,process,thread;
    ProcessIdentity identity;
};
// Launches the relay under test against a crafted anonymous mapping, exactly
// as launchCopy would deliver it.
bool launchRelay(const fs::path& relay,BootstrapData data,RelayChild& child) {
    ProcessIdentity self;
    if(!self.open(GetCurrentProcessId()))return false;
    HANDLE rawParent=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),self.handle(),GetCurrentProcess(),&rawParent,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,TRUE,0))return false;
    child.inheritedParent.reset(rawParent);
    data.parentHandle=reinterpret_cast<uint64_t>(child.inheritedParent.get());data.parent=self.stamp();
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(BootstrapData),nullptr));
    if(!mapping)return false;
    auto view=MapViewOfFile(mapping.get(),FILE_MAP_WRITE,0,0,sizeof(BootstrapData));
    if(!view)return false;
    std::memcpy(view,&data,sizeof(data));UnmapViewOfFile(view);
    HANDLE rawMapping=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),mapping.get(),GetCurrentProcess(),&rawMapping,FILE_MAP_READ,TRUE,0))return false;
    child.inheritedMapping.reset(rawMapping);
    std::wstring command=L"\""+relay.wstring()+L"\" "+std::to_wstring(reinterpret_cast<uint64_t>(child.inheritedMapping.get()));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(relay.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    child.process.reset(process.hProcess);child.thread.reset(process.hThread);
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(child.process.get(),&created,&exited,&kernel,&user))return false;
    const uint64_t stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),child.process.get(),GetCurrentProcess(),&limited,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return false;
    return child.identity.adopt(limited,{process.dwProcessId,stamp});
}
// GUI half of the handshake: the Coordinator::authenticate sequence driven by
// hand so the harness can time every step of the relay against it.
bool guiHandshake(LocalChannel& server,const RelayChild& child,const BootstrapData& data,Deadline deadline) {
    if(!server.accept(child.identity,deadline))return false;
    const auto hello=server.receive(deadline);
    if(!hello || hello->kind!=MessageKind::Hello || hello->transaction!=data.transaction
        || hello->token!=data.token)return false;
    return server.send({MessageKind::Ready,data.transaction,data.token},deadline);
}
// Fail-closed paths only: the harness itself plays the GUI, because no path
// under test ever reaches the Proceed gate.
int runRelay(const fs::path& relay,const RelayRequest& request) {
    const auto data=makeBootstrap(request);
    LocalChannel server;
    if(!server.create(data.transaction))return -1;
    RelayChild child;
    if(!launchRelay(relay,data,child))return -2;
    if(!guiHandshake(server,child,data,after(15000)))return -3;
    if(WaitForSingleObject(child.process.get(),30000)!=WAIT_OBJECT_0) {
        TerminateProcess(child.process.get(),1);return -4;
    }
    DWORD code=0;
    return GetExitCodeProcess(child.process.get(),&code)?static_cast<int>(code):-5;
}
// Stand-in for the GUI in the full sequence. It must be a separate process:
// the relay gates Proceed on the real exit of its bootstrap parent, so the
// harness itself can never play the application. Reached through --gui-parent.
int runGuiParent(wchar_t** arguments) {
    const fs::path relay=arguments[0],base=arguments[7];
    const std::wstring record=arguments[6],digest=arguments[5];
    RelayRequest request;
    request.installerPath=arguments[1];request.installRoot=arguments[2];request.dataDirectory=arguments[3];
    request.packageSize=std::wcstoull(arguments[4],nullptr,10);
    if(digest.size()!=64)return 3;
    for(std::size_t i=0;i<32;++i)
        request.packageSha256[i]=static_cast<uint8_t>(std::wcstoul(digest.substr(i*2,2).c_str(),nullptr,16));
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",record.c_str());
    SetEnvironmentVariableW(L"ZZLOGG_FIXTURE_ENGINE",L"light");
    SetEnvironmentVariableW(L"ZZLOGG_FIXTURE_GUI",L"ok");
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    const auto data=makeBootstrap(request);
    LocalChannel server;
    if(!server.create(data.transaction))return 4;
    RelayChild child;
    if(!launchRelay(relay,data,child))return 5;
    publish(base/L"relay-pid.txt",{std::to_wstring(child.identity.stamp().pid)});
    if(!guiHandshake(server,child,data,after(15000)))return 6;
    const auto awaiting=server.receive(after(120000));
    if(!awaiting || awaiting->kind!=MessageKind::AwaitingAppExit)return 7;
    // What the engine had already observed at the moment the relay asked this
    // process to commit its exit.
    publish(base/L"snapshot.txt",readLines(record));
    if(!server.send({MessageKind::CommitExit,data.transaction,data.token},after(5000)))return 8;
    Sleep(ApplicationLingerMs);
    // A real process death, not a message: this is what opens the gate.
    ExitProcess(0);
}
DWORD readPid(const fs::path& path) {
    const auto lines=readLines(path);
    return lines.empty()?0:std::wcstoul(lines.front().c_str(),nullptr,10);
}
// Drives the full sequence through a separate GUI stand-in process and
// returns the relay's own exit code; the relay outlives that stand-in.
int runRelayChain(const fs::path& relay,const fs::path& root,const RelayRequest& request,RelayObservation& observed) {
    const auto base=root/L"chain";
    const auto record=base/L"record.txt";
    wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
    if(!length || length>=32768)return -1;
    const std::wstring image(self,length);
    std::wstring command=L"\""+image+L"\" --gui-parent \""+relay.wstring()+L"\" \""+request.installerPath
        +L"\" \""+request.installRoot+L"\" \""+request.dataDirectory+L"\" "
        +std::to_wstring(request.packageSize)+L" "+digestText(request.packageSha256)
        +L" \""+record.wstring()+L"\" \""+base.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return -2;
    Handle gui(process.hProcess),guiThread(process.hThread);
    // The relay outlives its bootstrap parent, so its handle must be held
    // before that parent dies.
    Handle relayProcess;
    for(int tries=0;tries<600 && !relayProcess;++tries) {
        if(const auto pid=readPid(base/L"relay-pid.txt"))
            relayProcess.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
        if(!relayProcess)Sleep(ObservationPollMs);
    }
    if(!relayProcess)return -3;
    // The gate assertion: the engine may never observe Proceed while the
    // process playing the application is still alive. Reading the record
    // before the liveness probe is what makes a hit conclusive.
    bool proceedWhileAlive=false;
    for(;;) {
        const bool proceeded=hasLine(readLines(record),L"kind=8");
        const bool alive=WaitForSingleObject(gui.get(),0)==WAIT_TIMEOUT;
        proceedWhileAlive=proceedWhileAlive || (proceeded && alive);
        if(!alive)break;
        Sleep(ObservationPollMs);
    }
    bool proceeded=false;
    for(int tries=0;tries<1200 && !proceeded;++tries) {
        proceeded=hasLine(readLines(record),L"kind=8");
        if(!proceeded)Sleep(ObservationPollMs);
    }
    observed.proceedAfterApplicationExit=proceeded && !proceedWhileAlive;
    const auto snapshot=readLines(base/L"snapshot.txt");
    observed.awaitingBeforeCommit=hasLine(snapshot,L"kind=2") && !hasLine(snapshot,L"kind=4");
    DWORD guiCode=1;
    if(!GetExitCodeProcess(gui.get(),&guiCode) || guiCode)return -100-static_cast<int>(guiCode);
    if(WaitForSingleObject(relayProcess.get(),120000)!=WAIT_OBJECT_0)return -4;
    DWORD code=0;
    return GetExitCodeProcess(relayProcess.get(),&code)?static_cast<int>(code):-5;
}
// Registered-installation stand-in: real marker, manifest magic and the
// fixture renamed to the production executable name.
void makeInstallRoot(const fs::path& dir,const fs::path& fixtureExe) {
    fs::create_directories(dir);
    {std::ofstream marker(dir/L".zzlogg-install-root",std::ios::binary|std::ios::trunc);marker<<"ZzLogg 2.0.0\r\n";}
    {std::ofstream manifest(dir/L".zzlogg-files.manifest",std::ios::binary|std::ios::trunc);
      manifest<<"ZZTXMAN1";for(int i=0;i<8;++i)manifest<<'\0';}
    fs::copy_file(fixtureExe,dir/L"ZzLogg.exe",fs::copy_options::overwrite_existing);
}
// The restarted GUI stand-in keeps the installation tree leased until it ends.
void awaitRestartedGui(const fs::path& record) {
    for(const auto& line:readLines(record)) {
        if(line.rfind(L"gui pid=",0)!=0)continue;
        Handle gui(OpenProcess(SYNCHRONIZE,FALSE,std::wcstoul(line.substr(8).c_str(),nullptr,10)));
        if(gui)WaitForSingleObject(gui.get(),15000);
    }
}
// argv[1] = the relay under test (production ZzLoggUpdate.exe or
// zzlogg_updater_relayfixture.exe), argv[2] = the executable the fixture
// launcher runs as the installer/engine.
int runRelayTest(int argc,wchar_t** argv) {
    if(argc==10 && std::wstring(argv[1])==L"--gui-parent")return runGuiParent(argv+2);
    if(argc!=3)return 2;
    int failures=0;
    auto check=[&](bool ok,const char* name){
        if(!ok){++failures;std::cerr<<"FAIL: "<<name<<" error="<<GetLastError()<<'\n';}};
    const auto root=createTestRoot(testTempDirectory());
    // Both binaries carry package leases that compare the normalised path
    // against the requested one, so both run from inside the test root.
    const auto relay=root/fs::path(argv[1]).filename();
    fs::copy_file(fs::path(argv[1]).make_preferred(),relay);
    const auto installer=root/L"fixture.exe";
    fs::copy_file(fs::path(argv[2]).make_preferred(),installer);

    // A bootstrap without the installer-chain fields is a protocol handshake
    // only; there is no installation to execute.
    check(runRelay(relay,{})==ExecutionDisabled,
        "bootstrap without installer fields disables execution");

    // The declared bytes disagree with what is on disk: fail closed before
    // any installer is launched.
    RelayRequest mismatched;
    mismatched.installerPath=installer.wstring();
    mismatched.installRoot=(root/L"install").wstring();
    mismatched.packageSize=fs::file_size(installer)+1;
    mismatched.packageSha256=fileSha256(installer);
    check(runRelay(relay,mismatched)==PackageRejected,
        "declared size mismatch is rejected before any launch");
    auto wrongDigest=mismatched;
    wrongDigest.packageSize=fs::file_size(installer);
    wrongDigest.packageSha256[0]^=1;
    check(runRelay(relay,wrongDigest)==PackageRejected,
        "declared digest mismatch is rejected before any launch");

    if(relay.stem()==L"zzlogg_updater_relayfixture") {
        // Full relay sequence: AwaitingAppExit is forwarded only once the
        // engine is waiting, and Proceed only after the GUI really exited.
        // The production binary is excluded because it would raise real UAC.
        const auto chain=root/L"chain";
        fs::create_directories(chain);
        RelayRequest good;
        good.installerPath=installer.wstring();
        good.installRoot=(chain/L"install").wstring();
        good.dataDirectory=(chain/L"data").wstring();
        good.packageSize=fs::file_size(installer);
        good.packageSha256=fileSha256(installer);
        makeInstallRoot(good.installRoot,installer);
        fs::create_directories(good.dataDirectory);
        // An elevated harness never restarts the application as the original
        // user; the completed transaction reports the pending restart instead.
        ProcessIdentity self;self.open(GetCurrentProcessId());
        const int completed=self.elevated()?RestartPending:0;
        RelayObservation observed;
        check(runRelayChain(relay,root,good,observed)==completed,"fixture relay completes the chain");
        check(observed.awaitingBeforeCommit,
            "AwaitingAppExit reached the GUI only after the engine was waiting");
        check(observed.proceedAfterApplicationExit,
            "Proceed was released only after the application process really exited");
        awaitRestartedGui(chain/L"record.txt");
    }
    std::error_code ec;fs::remove_all(root,ec);
    std::cout<<(failures?"relay FAILED":"relay ok")<<std::endl;
    return failures?1:0;
}
}
int wmain(int argc,wchar_t** argv) {
    try {return runRelayTest(argc,argv);}
    catch(const std::exception& error) {
        std::cerr<<"FAIL: relay test exception: "<<error.what()<<std::endl;
        return 1;
    }
}
