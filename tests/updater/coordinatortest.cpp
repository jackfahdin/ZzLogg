#include "coordinator_p.h"
#include "installationactivity_win.h"
#include "installlock_win.h"
#include "updatertesthelpers.h"
#include <sddl.h>
#include <aclapi.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
namespace {
fs::path createTestRoot(const fs::path& base) {
    std::array<unsigned char,16> nonce{};
    if(!randomBytes(nonce.data(),static_cast<ULONG>(nonce.size())))
        throw std::runtime_error("cannot generate a unique coordinator test directory");
    std::wstring name=L"ZzLogg-coordinate-test-"+std::to_wstring(GetCurrentProcessId())+L"-";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto byte:nonce){name+=hex[byte>>4];name+=hex[byte&15];}
    auto root=base/name;
    // Atomic creation must succeed: never adopt or erase another run's files.
    if(!fs::create_directory(root))
        throw fs::filesystem_error("coordinator test directory already exists",root,
            std::make_error_code(std::errc::file_exists));
    return root;
}
DWORD probeEnter(const fs::path& dir) {
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --probe-enter \""+dir.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 999;
    Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),10000)!=WAIT_OBJECT_0){TerminateProcess(child.get(),998);return 998;}
    DWORD code=999;GetExitCodeProcess(child.get(),&code);return code;
}
// Application stand-in: a real independent process with a bounded lifetime so
// the coordinator Proceed gate observes a genuine process exit.
struct AppStandin { Handle process,thread; ProcessIdentity identity; };
AppStandin launchAppStandin(const fs::path& fixtureExe,DWORD milliseconds) {
    AppStandin standin;
    std::wstring command=L"\""+fixtureExe.wstring()+L"\" --app "+std::to_wstring(milliseconds);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(fixtureExe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return standin;
    standin.process.reset(process.hProcess);standin.thread.reset(process.hThread);
    standin.identity.open(process.dwProcessId);
    return standin;
}
// Polls the Proceed gate until the stand-in really exits; any result other
// than PeerRunning/ProceedSent surfaces immediately.
CoordinationResult proceedWhenExited(Coordinator& coordinator,const Handle& appProcess) {
    for(int tries=0;tries<100;++tries) {
        const auto result=coordinator.proceedIfExited(after(200));
        if(result!=CoordinationResult::PeerRunning)return result;
        WaitForSingleObject(appProcess.get(),100);
    }
    return CoordinationResult::PeerRunning;
}
// Launches the fixture against a crafted anonymous mapping, exactly as
// launchCopy would deliver it; returns the fixture exit code.
template<class Mapping> DWORD spawnMappedFixture(const fs::path& fixtureExe,Mapping& data) {
    ProcessIdentity self;if(!self.open(GetCurrentProcessId()))return 998;
    HANDLE rawParent=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),self.handle(),GetCurrentProcess(),&rawParent,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,TRUE,0))return 998;
    Handle inheritedParent(rawParent);
    data.parentHandle=reinterpret_cast<uint64_t>(inheritedParent.get());data.parent=self.stamp();
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Mapping),nullptr));
    if(!mapping)return 998;
    auto view=MapViewOfFile(mapping.get(),FILE_MAP_WRITE,0,0,sizeof(Mapping));if(!view)return 998;
    std::memcpy(view,&data,sizeof(Mapping));UnmapViewOfFile(view);
    HANDLE rawMapping=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),mapping.get(),GetCurrentProcess(),&rawMapping,FILE_MAP_READ,TRUE,0))return 998;
    Handle inheritedMapping(rawMapping);
    std::wstring command=L"\""+fixtureExe.wstring()+L"\" "+std::to_wstring(reinterpret_cast<uint64_t>(inheritedMapping.get()));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(fixtureExe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 998;
    Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),10000)!=WAIT_OBJECT_0){TerminateProcess(child.get(),997);return 997;}
    DWORD code=997;GetExitCodeProcess(child.get(),&code);return code;
}
std::vector<std::wstring> recordLines(const fs::path& path) {
    std::vector<std::wstring> lines;std::wifstream record(path);std::wstring line;
    while(std::getline(record,line))lines.push_back(line);
    return lines;
}
struct Ace { std::wstring sid;DWORD mask; };
// Real DACL read-back of a written credential file, mirroring the journal
// test's assertion style: protected, exactly the expected ACE set.
std::vector<Ace> fileDacl(const fs::path& path,bool& protectedDacl) {
    protectedDacl=false;std::vector<Ace> result;
    PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        nullptr,nullptr,&acl,nullptr,&descriptor)!=ERROR_SUCCESS) return result;
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;
    if(GetSecurityDescriptorControl(descriptor,&control,&revision)) protectedDacl=(control&SE_DACL_PROTECTED)!=0;
    if(acl) for(DWORD i=0;i<acl->AceCount;++i) {
        void* entry=nullptr;if(!GetAce(acl,i,&entry)) continue;
        const auto* header=static_cast<ACE_HEADER*>(entry);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE){result.push_back({L"<deny>",0});continue;}
        const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        LPWSTR sid=nullptr;
        if(ConvertSidToStringSidW(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)),&sid)){result.push_back({sid,ace->Mask});LocalFree(sid);}
    }
    if(descriptor)LocalFree(descriptor);
    return result;
}
// The locator acceptance is the engine's parseTxid itself (txcontract_win_p.h):
// exactly 16 lowercase hexadecimal digits, nonzero.
bool locatorShape(const std::wstring& locator) {
    std::uint64_t value=0;
    return parseTxid(locator,value);
}
// Injected installer launcher: models ShellExecuteEx runas by launching the
// fixture installer as an ordinary child and adopting its real identity. The
// recorded arguments let the harness pin the restricted-switch contract.
struct LaunchCapture { std::wstring installer,arguments;LaunchError behavior=LaunchError::None;DWORD launchedPid=0; };
LauncherOutcome fixtureLaunch(LaunchCapture& capture,const std::wstring& installer,const std::wstring& restrictedSwitch) {
    capture.installer=installer;capture.arguments=restrictedSwitch;capture.launchedPid=0;
    LauncherOutcome outcome;outcome.error=capture.behavior;
    if(capture.behavior!=LaunchError::None) return outcome;
    std::wstring command=L"\""+installer+L"\" "+restrictedSwitch;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(installer.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return outcome;
    Handle thread(process.hThread),launched(process.hProcess);
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(launched.get(),&created,&exited,&kernel,&user))return outcome;
    const uint64_t stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),launched.get(),GetCurrentProcess(),&limited,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return outcome;
    ProcessIdentity identity;
    if(!identity.adopt(limited,{process.dwProcessId,stamp}))return outcome;
    capture.launchedPid=process.dwProcessId;
    outcome.process=std::move(identity);outcome.error=LaunchError::None;return outcome;
}
DWORD spawnArgs(const fs::path& exe,const std::wstring& arguments,DWORD timeoutMs=30000) {
    std::wstring command=L"\""+exe.wstring()+L"\" "+arguments;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 999;
    Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),timeoutMs)!=WAIT_OBJECT_0){TerminateProcess(child.get(),998);return 998;}
    DWORD code=999;GetExitCodeProcess(child.get(),&code);return code;
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
bool waitPidExit(DWORD pid,DWORD milliseconds) {
    Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));
    return process && WaitForSingleObject(process.get(),milliseconds)==WAIT_OBJECT_0;
}
DWORD guiPidFromRecord(const std::vector<std::wstring>& lines) {
    for(const auto& line:lines)
        if(line.rfind(L"gui pid=",0)==0) return std::wcstoul(line.substr(8).c_str(),nullptr,10);
    return 0;
}
bool recordHasGui(const std::vector<std::wstring>& lines) {
    for(const auto& line:lines) if(line.rfind(L"gui pid=",0)==0) return true;
    return false;
}
void clearChainEnv() {
    for(const auto* name:{L"ZZLOGG_HANDOFF_RECORD",L"ZZLOGG_FIXTURE_ENGINE",L"ZZLOGG_FIXTURE_GUI"})
        SetEnvironmentVariableW(name,nullptr);
}
// Drives the installer chain to a completed transaction: real reservation,
// fixture launcher, credential-file engine, real application exit, Proceed
// gate, and a final result of Complete (1) or ManualRestartRequired (2) on an
// elevated harness; 0 on any failure.
struct ChainDrive {
    InstallationActivity activity;DirectoryIdentity reserved{};
    AppStandin app;Coordinator coordinator;LaunchCapture capture;
    fs::path installRoot,recordPath,dataDir;
};
int driveChainToComplete(ChainDrive& drive,const fs::path& fixtureExe,const fs::path& base,
    const std::wstring& name,const wchar_t* guiMode) {
    drive.installRoot=base/(name+L"-install");drive.recordPath=base/(name+L"-record.txt");drive.dataDir=base/(name+L"-data");
    makeInstallRoot(drive.installRoot,fixtureExe);fs::create_directory(drive.dataDir);
    if(drive.activity.enter(drive.installRoot.wstring())!=ActivityError::None
        || drive.activity.reserveUpdate()!=ActivityError::None) return 0;
    drive.reserved=drive.activity.identity();
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",drive.recordPath.c_str());
    SetEnvironmentVariableW(L"ZZLOGG_FIXTURE_ENGINE",L"light");
    if(guiMode) SetEnvironmentVariableW(L"ZZLOGG_FIXTURE_GUI",guiMode);
    drive.app=launchAppStandin(fixtureExe,1500);
    if(!drive.app.identity.handle()) return 0;
    CoordinatorOptions options;options.requireElevatedPeer=false;
    options.launcher=[&](const std::wstring& installer,const std::wstring& restrictedSwitch) {
        return fixtureLaunch(drive.capture,installer,restrictedSwitch); };
    LaunchError error=LaunchError::Failed;
    if(!drive.coordinator.startInstaller({fixtureExe.wstring(),drive.installRoot.wstring(),drive.dataDir.wstring()},
            &drive.reserved,&drive.app.identity,options,&error) || error!=LaunchError::None) return 0;
    if(!drive.coordinator.authenticate(after(3000))
        || drive.coordinator.awaitAppExit(after(3000))!=CoordinationResult::WaitingForAppExit
        || !drive.coordinator.commitExit(after(500))
        || proceedWhenExited(drive.coordinator,drive.app.process)!=CoordinationResult::ProceedSent) return 0;
    const auto finished=drive.coordinator.finish(after(5000));
    return finished==CoordinationResult::Complete?1:finished==CoordinationResult::ManualRestartRequired?2:0;
}
}
int runCoordinatorTest(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[1])==L"--probe-enter"){
        // Third-process entry probe: 0 entered, 10 blocked/unavailable.
        InstallationActivity activity;
        return activity.enter(argv[2])==ActivityError::None?0:10;
    }
    if(argc==5 && std::wstring(argv[1])==L"--reserve-parent"){
        // Holds a real update reservation, hands the directory identity to the
        // child coordinator, reaches WaitingForAppExit, then truly vanishes
        // without releasing leases so gate continuity is observable.
        InstallationActivity activity;
        if(activity.enter(argv[4])!=ActivityError::None)return 4;
        if(activity.reserveUpdate()!=ActivityError::None)return 5;
        const auto identity=activity.identity();
        Coordinator c;
        if(!c.start(argv[2],argv[3],&identity) || !c.authenticate(after(2000))
            || c.awaitAppExit(after(2000))!=CoordinationResult::WaitingForAppExit)return 6;
        {std::wofstream info(fs::path(argv[3])/L"reserve-info.txt");info<<c.process().stamp().pid<<L'\n'<<c.runtimePath()<<L'\n';}
        ExitProcess(0);
    }
    if(argc==4 && std::wstring(argv[1])==L"--orphan-parent"){
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"orphan");Coordinator c;
        if(!c.start(fs::path(argv[2]).make_preferred().wstring(),argv[3]) || !c.authenticate(after(2000))
            || c.awaitAppExit(after(2000))!=CoordinationResult::WaitingForAppExit)return 3;
        {std::wofstream info(fs::path(argv[3])/L"orphan-info.txt");info<<c.process().stamp().pid<<L'\n'<<c.runtimePath()<<L'\n';}
        ExitProcess(0); // The parent vanishes without releasing leases in user-mode destructors.
    }
    if(argc!=3)return 2;int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL: "<<name<<" error="<<GetLastError()<<'\n';}};
    const auto temp=detail::testTempDirectory();
    auto root=createTestRoot(temp);
    // A previous run's files must never be reused, even when Windows reuses its PID.
    const auto firstRoot=createTestRoot(root);
    {std::ofstream sentinel(firstRoot/L"previous-run.txt");sentinel<<"preserve previous run";}
    const auto secondRoot=createTestRoot(root);
    check(firstRoot!=secondRoot && !fs::exists(secondRoot/L"previous-run.txt"),"each test run gets an independent directory despite the same PID");
    std::string previous;{std::ifstream sentinel(firstRoot/L"previous-run.txt");std::getline(sentinel,previous);}
    check(previous=="preserve previous run","allocating another test root preserves previous files");
    fs::remove(firstRoot/L"previous-run.txt");fs::remove(firstRoot);fs::remove(secondRoot);
    if(failures){fs::remove(root);return 1;}
    fs::create_directory(root/L"install");fs::create_directory(root/L"runtime");
    const auto source=root/L"install"/L"fixture.exe";fs::copy_file(argv[1],source);
    for(auto mode:{L"success",L"linger",L"early",L"timeout",L"token",L"order",L"replay",L"cancel",L"hello-only",L"exit-after-hello"}) {
        std::wcout<<L"coordinator fixture: "<<mode<<std::endl;
        std::wstring m=mode;
        const bool gated=m==L"success" || m==L"linger";
        AppStandin app;
        if(gated) app=launchAppStandin(fs::path(argv[1]).make_preferred(),2500);
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",mode);Coordinator coordinator;
        check(!gated || app.identity.handle(),"application stand-in identity held");
        check(coordinator.start(source.wstring(),(root/L"runtime").wstring(),nullptr,
            gated?&app.identity:nullptr),"fixture starts through real runtime copy");
        if(!coordinator.process().handle())continue;
        check(!coordinator.canCommitExit(),"process creation never authorizes exit");
        wchar_t image[32768]{};DWORD length=32768;
        check(QueryFullProcessImageNameW(coordinator.process().handle(),0,image,&length) && fs::path(image)==fs::path(coordinator.runtimePath()),"actual child executes new runtime path");
        const auto authenticated=coordinator.authenticate(after(250));
        if(m==L"early" || m==L"timeout" || m==L"token" || m==L"order") {
            check(!authenticated && !coordinator.canCommitExit(),"bad or absent Hello refuses exit");continue;
        }
        check(authenticated && !coordinator.canCommitExit(),"Hello authenticates but cannot exit");
        auto waiting=coordinator.awaitAppExit(after(200));
        if(!gated) {
            check(waiting!=CoordinationResult::WaitingForAppExit && !coordinator.canCommitExit(),"replay cancel ready-only and early death refuse exit");continue;
        }
        check(waiting==CoordinationResult::WaitingForAppExit && coordinator.canCommitExit(),"only live authenticated AwaitingAppExit authorizes exit");
        check(MoveFileW(source.c_str(),(root/L"install"/L"old.exe").c_str()),"original file renames while runtime process is alive");
        fs::copy_file(argv[1],source);fs::remove(root/L"install"/L"old.exe");
        check(coordinator.commitExit(after(500)),"commit sent only after guard");
        check(coordinator.proceedIfExited(after(200))==CoordinationResult::PeerRunning,"Proceed withheld while the application still runs");
        check(proceedWhenExited(coordinator,app.process)==CoordinationResult::ProceedSent,"Proceed sent only after real application exit");
        if(m==L"linger")check(coordinator.finish(after(50))==CoordinationResult::PeerRunning,"Complete message cannot substitute actual process exit");
        auto completed=coordinator.finish(after(2000));ProcessIdentity self;self.open(GetCurrentProcessId());
        check(completed==(self.elevated()?CoordinationResult::ManualRestartRequired:CoordinationResult::Complete),"final exit observed and elevation restart policy applied");
    }
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    {
        // Proceed gate ordering, observed through the fixture record: Ready,
        // CommitExit and Proceed must arrive in order, Proceed only after the
        // stand-in's real exit, and the bootstrap carries the data directory.
        std::cout<<"coordinator proceed gate ordering"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");
        const auto recordPath=root/L"proceed-record.txt";
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",recordPath.c_str());
        fs::create_directory(root/L"data");const auto dataDirectory=(root/L"data").wstring();
        auto app=launchAppStandin(fs::path(argv[1]).make_preferred(),1200);Coordinator c;
        check(app.identity.handle() && c.start(source.wstring(),(root/L"runtime").wstring(),nullptr,&app.identity,dataDirectory)
            && c.authenticate(after(2000)) && c.awaitAppExit(after(2000))==CoordinationResult::WaitingForAppExit
            && c.commitExit(after(500)),"gate chain reaches committed exit with a data directory");
        check(c.proceedIfExited(after(200))==CoordinationResult::PeerRunning,"Proceed withheld while the application still runs");
        check(proceedWhenExited(c,app.process)==CoordinationResult::ProceedSent,"Proceed sent immediately after real exit");
        check(c.proceedIfExited(after(200))==CoordinationResult::ProceedSent,"Proceed is never repeated");
        ProcessIdentity self;self.open(GetCurrentProcessId());
        check(c.finish(after(3000))==(self.elevated()?CoordinationResult::ManualRestartRequired:CoordinationResult::Complete),
            "gated transaction completes after Proceed");
        const std::vector<std::wstring> expected{L"datadir="+dataDirectory,L"kind=2",L"kind=4",L"kind=8"};
        check(recordLines(recordPath)==expected,"fixture observed Ready CommitExit Proceed in order with the data directory");
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",nullptr);
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    }
    {
        // Fail-closed latch ordering: once the coordinator has aborted, the
        // Proceed latch must never surface ProceedSent again.
        std::cout<<"coordinator proceed latch after abort"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");
        auto app=launchAppStandin(fs::path(argv[1]).make_preferred(),1200);Coordinator c;
        check(app.identity.handle() && c.start(source.wstring(),(root/L"runtime").wstring(),nullptr,&app.identity)
            && c.authenticate(after(2000)) && c.awaitAppExit(after(2000))==CoordinationResult::WaitingForAppExit
            && c.commitExit(after(500))
            && proceedWhenExited(c,app.process)==CoordinationResult::ProceedSent,"abort-order chain reaches ProceedSent");
        c.cancel(after(100));
        check(c.proceedIfExited(after(100))==CoordinationResult::Failed,"aborted coordinator never reports ProceedSent again");
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    }
    {
        // Contract violation: the engine pretends to modify the installation
        // and reports Complete without waiting for the Proceed gate.
        std::cout<<"coordinator proceed violation"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"early-complete");
        const auto recordPath=root/L"violation-record.txt";
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",recordPath.c_str());
        Coordinator c;
        check(c.start(source.wstring(),(root/L"runtime").wstring()) && c.authenticate(after(2000))
            && c.awaitAppExit(after(2000))==CoordinationResult::WaitingForAppExit && c.commitExit(after(500)),
            "violation chain reaches committed exit");
        check(c.finish(after(2000))==CoordinationResult::Failed && !c.canCommitExit(),
            "Complete before Proceed is rejected and aborts");
        const auto lines=recordLines(recordPath);
        check(std::find(lines.begin(),lines.end(),L"write")!=lines.end()
            && std::find(lines.begin(),lines.end(),L"kind=8")==lines.end(),
            "pretend write before Proceed is never gated through");
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",nullptr);
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    }
    {SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");Coordinator c;
      check(c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring()) && c.authenticate(after(2000)),
          "proceed-order fixture authenticates");
      check(c.proceedIfExited(after(100))==CoordinationResult::Failed && !c.canCommitExit(),"Proceed before CommitExit aborts");}
    {SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");Coordinator c;
      check(c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring()) && c.authenticate(after(2000))
        && c.awaitAppExit(after(2000))==CoordinationResult::WaitingForAppExit && c.commitExit(after(500)),"committed exit without application identity");
      check(c.proceedIfExited(after(200))==CoordinationResult::Failed,"Proceed fail-closed without the application handle");}
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    {
        std::cout<<"coordinator deferred cleanup"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"linger");
        auto app=launchAppStandin(fs::path(argv[1]).make_preferred(),1500);
        ULONGLONG beforeDestruction=0;std::wstring path;ProcessIdentity observer;
        {Coordinator c;check(app.identity.handle()
            && c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring(),nullptr,&app.identity)
            && c.authenticate(after(2000)) && c.awaitAppExit(after(1000))==CoordinationResult::WaitingForAppExit
            && c.commitExit(after(500))
            && proceedWhenExited(c,app.process)==CoordinationResult::ProceedSent,"lingering ownership fixture starts");
          check(c.finish(after(30))==CoordinationResult::PeerRunning,"lingering fixture still running");
          path=c.runtimePath();observer.open(c.process().stamp().pid);beforeDestruction=GetTickCount64();}
        check(GetTickCount64()-beforeDestruction<150,"destruction after timeout must not block caller on live child");
        if(observer.alive())check(!DeleteFileW(path.c_str()),"deferred owner keeps runtime file leased");
        WaitForSingleObject(observer.handle(),2000);
        for(int tries=0;tries<100 && fs::exists(path);++tries)Sleep(10);
        check(!fs::exists(path),"deferred owner cleans only after real child exit");
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    }
    {SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");Coordinator c;
      check(c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring()) && !c.commitExit(after(500)),"commit before handshake is rejected");}
    {SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");Coordinator c;
      check(c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring()) && c.authenticate(after(2000)),"cancel fixture starts");
      c.cancel(after(100));check(!c.canCommitExit(),"caller cancellation revokes exit permission");}
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    {
        std::cout<<"coordinator orphan parent"<<std::endl;
        const auto orphanBase=root/L"orphan-base";fs::create_directory(orphanBase);
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        std::wstring command=L"\""+std::wstring(executable)+L"\" --orphan-parent \""+argv[1]+L"\" \""+orphanBase.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"orphan parent starts");
        Handle parent(process.hProcess),thread(process.hThread);check(WaitForSingleObject(parent.get(),5000)==WAIT_OBJECT_0,"orphan parent truly exits");
        DWORD pid=0;std::wstring path;{std::wifstream info(orphanBase/L"orphan-info.txt");info>>pid;info.ignore();std::getline(info,path);}
        Handle orphan(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
        check(orphan && WaitForSingleObject(orphan.get(),0)==WAIT_TIMEOUT,"child outlives lost parent");
        check(orphan && WaitForSingleObject(orphan.get(),100)==WAIT_TIMEOUT,"orphan stays alive while lease assertions execute");
        if(orphan && !path.empty()){
            Handle write(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
            check(!write && !DeleteFileW(path.c_str()),"orphan retains own executable lease");
            check(!MoveFileW(fs::path(path).parent_path().c_str(),fs::path(path+L"-moved").c_str()),"orphan retains ancestor lease");
            check(!MoveFileW(orphanBase.c_str(),(root/L"orphan-redirect").c_str()),"orphan retains runtime base ancestor lease");
            check(WaitForSingleObject(orphan.get(),3000)==WAIT_OBJECT_0,"orphan fails closed and exits");
            write.reset();std::error_code cleanup;fs::remove(path,cleanup);fs::remove(fs::path(path).parent_path(),cleanup);
        }
        std::error_code cleanup;fs::remove(orphanBase/L"orphan-info.txt",cleanup);fs::remove(orphanBase,cleanup);
    }
    {Coordinator c;check(!c.start((root/L"missing.exe").wstring(),root.wstring()) && !c.canCommitExit(),"launch failure refuses exit");}
    {const auto invalid=root/L"install"/L"invalid.exe";{std::ofstream file(invalid);file<<"not a PE executable";}
      std::cout<<"coordinator invalid image"<<std::endl;
      Coordinator c;check(!c.start(invalid.wstring(),(root/L"runtime").wstring()) && !c.canCommitExit(),"CreateProcess failure refuses exit");fs::remove(invalid);}
    {Coordinator c;check(c.start(fs::path(argv[2]).make_preferred().wstring(),root.wstring()) && c.authenticate(after(2000)),"production bootstrap authenticates");
      check(c.awaitAppExit(after(1000))==CoordinationResult::Failed && !c.canCommitExit(),"production execution disabled");
      WaitForSingleObject(c.process().handle(),2000);DWORD code=0;GetExitCodeProcess(c.process().handle(),&code);check(code==40,"production returns explicit ExecutionDisabled");}
    {
        // Directory reservation continuity across the reserving parent's real exit.
        std::cout<<"coordinator reservation continuity"<<std::endl;
        const auto reserved=root/L"reserved-install";fs::create_directory(reserved);
        const auto reserveBase=root/L"reserve-runtime";fs::create_directory(reserveBase);
        DirectoryIdentity unreserved{};
        check(InstallationActivity::probeIdentity(reserved.wstring(),unreserved),"identity probe of an unreserved directory");
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"success");
        {Coordinator c;
          check(c.start(source.wstring(),(root/L"runtime").wstring(),&unreserved),"launch with unmatched directory identity still starts");
          check(!c.authenticate(after(2000)) && !c.canCommitExit(),"child without the existing reservation mutex never completes Hello");
          WaitForSingleObject(c.process().handle(),2000);DWORD code=0;GetExitCodeProcess(c.process().handle(),&code);
          check(code==51,"unmatched directory child returns BootstrapRejected");}
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"orphan");
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        std::wstring command=L"\""+std::wstring(executable)+L"\" --reserve-parent \""+source.wstring()+L"\" \""+reserveBase.wstring()+L"\" \""+reserved.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"reserve parent starts");
        Handle parent(process.hProcess),parentThread(process.hThread);
        check(WaitForSingleObject(parent.get(),10000)==WAIT_OBJECT_0,"reserve parent truly exits");
        DWORD parentCode=1;GetExitCodeProcess(parent.get(),&parentCode);
        check(parentCode==0,"reserve parent reached WaitingForAppExit before exiting");
        DWORD childPid=0;std::wstring childPath;{std::wifstream info(reserveBase/L"reserve-info.txt");info>>childPid;info.ignore();std::getline(info,childPath);}
        Handle grandchild(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,childPid));
        check(grandchild && WaitForSingleObject(grandchild.get(),0)==WAIT_TIMEOUT,"observing child outlives the reserving parent");
        check(probeEnter(reserved)==10,"third-process entry stays rejected after the reserving parent exited");
        {InstallLock lock;const auto claimResult=lock.acquire(reserved.wstring());
          // The reserving parent vanished without releasing: the object lives
          // on through the child's observer, so every new claim is fail-closed
          // (blocked or observed as abandoned), never acquired.
          check(claimResult==InstallLockError::Blocked || claimResult==InstallLockError::Abandoned,
              "install lock stays fail-closed while the observer holds the object");}
        check(WaitForSingleObject(grandchild.get(),8000)==WAIT_OBJECT_0,"observing child ends on its own");
        check(probeEnter(reserved)==0,"entry allowed again after the observing child ends");
        {InstallLock lock;check(lock.acquire(reserved.wstring())==InstallLockError::None,"install lock reacquired once the observer is gone");}
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
        std::error_code cleanup;fs::remove_all(reserveBase,cleanup);fs::remove(reserved,cleanup);
    }
    {
        // Bootstrap mapping v4: crafted mappings drive the real child bootstrap
        // parser directly. 52 = bootstrap passed (channel connect then fails);
        // 51 = BootstrapRejected.
        std::cout<<"coordinator bootstrap v4"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
        BootstrapData base{};
        randomBytes(base.transaction.data(),16);randomBytes(base.token.data(),32);
        check(spawnMappedFixture(source,base)==52,"valid v4 mapping passes bootstrap");
        auto retired=base;retired.version=3;
        check(spawnMappedFixture(source,retired)==51,"retired version 3 mapping rejected");
        auto datadir=base;{const auto path=root.wstring();std::copy(path.begin(),path.end(),datadir.dataDirectory);}
        check(spawnMappedFixture(source,datadir)==52,"absolute data directory accepted");
        auto control=base;{auto path=root.wstring();path[4]=wchar_t(1);std::copy(path.begin(),path.end(),control.dataDirectory);}
        check(spawnMappedFixture(source,control)==51,"control character in data directory rejected");
        auto relative=base;{const wchar_t text[]=L"relative\\path";std::copy(text,text+_countof(text),relative.dataDirectory);}
        check(spawnMappedFixture(source,relative)==51,"relative data directory rejected");
        auto unterminated=base;std::fill(std::begin(unterminated.dataDirectory),std::end(unterminated.dataDirectory),L'x');
        check(spawnMappedFixture(source,unterminated)==51,"unterminated overlong data directory rejected");
        auto installer=base;
        {const auto file=(root/L"setup.exe").wstring();std::copy(file.begin(),file.end(),installer.installerPath);
         const auto target=root.wstring();std::copy(target.begin(),target.end(),installer.installRoot);
         installer.packageSize=3;installer.packageSha256[0]=1;}
        check(spawnMappedFixture(source,installer)==52,"absolute installer path and install root accepted");
        auto relativeInstaller=installer;
        {const wchar_t text[]=L"relative\\setup.exe";
         std::fill(std::begin(relativeInstaller.installerPath),std::end(relativeInstaller.installerPath),L'\0');
         std::copy(text,text+_countof(text),relativeInstaller.installerPath);}
        check(spawnMappedFixture(source,relativeInstaller)==51,"relative installer path rejected");
        auto halfSet=installer;
        std::fill(std::begin(halfSet.installRoot),std::end(halfSet.installRoot),L'\0');
        check(spawnMappedFixture(source,halfSet)==51,"installer path without an install root rejected");
        auto zeroSize=installer;zeroSize.packageSize=0;
        check(spawnMappedFixture(source,zeroSize)==51,"installer fields without a package size rejected");
        auto oversize=installer;oversize.packageSize=512ull*1024*1024+1;
        check(spawnMappedFixture(source,oversize)==51,"package size beyond the lease limit rejected");
        auto zeroDigest=installer;
        std::fill(std::begin(zeroDigest.packageSha256),std::end(zeroDigest.packageSha256),uint8_t(0));
        check(spawnMappedFixture(source,zeroDigest)==51,"all-zero package digest rejected");
    }
    {
        // 3C installer chain: credential file + restricted switch launch of
        // the fixture installer, engine grandchild protocol, ordinary-user
        // restart of the fixture GUI with bounded endpoint confirmation.
        std::cout<<"coordinator installer chain"<<std::endl;
        const auto base=root/L"chain";
        ChainDrive drive;
        const auto chainResult=driveChainToComplete(drive,source,base,L"ok",L"ok");
        check(chainResult!=0,"installer chain reaches a completed transaction");
        if(chainResult!=0) {
            const auto locator=drive.capture.arguments.size()>15?drive.capture.arguments.substr(15):std::wstring();
            check(drive.capture.arguments.rfind(L"/ZzLoggUpgrade=",0)==0 && locatorShape(locator),
                "restricted switch carries only the 16-hex nonzero locator");
            check(drive.capture.installer==source.wstring(),"launcher received exactly the verified installer path");
            check(drive.capture.launchedPid && drive.coordinator.process().stamp().pid!=drive.capture.launchedPid,
                "channel client is the engine grandchild, never the installer itself");
            check(!fs::exists(credentialPath(locator)),"engine deleted the credential file after reading");
            {InstallLock lock;const auto claim=lock.acquire(drive.installRoot.wstring());
              check(claim==InstallLockError::Blocked || claim==InstallLockError::Abandoned,
                  "coordinator observer keeps the reservation gate closed across the chain");}
            if(chainResult!=1) {
                std::cout<<"elevated harness: restart covered by ManualRestartRequired semantics"<<std::endl;
            } else {
                check(drive.coordinator.restart(after(5000))==CoordinationResult::Restarted,
                    "fixture GUI restarted and endpoint-confirmed");
                const auto lines=recordLines(drive.recordPath);
                check(lines.size()==6 && lines[0]==L"installer switch="+drive.capture.arguments
                    && lines[1]==L"engine txid="+locator
                    && lines[2]==L"kind=2" && lines[3]==L"kind=4" && lines[4]==L"kind=8",
                    "fixture observed switch, engine bootstrap and the gated protocol in order");
                check(lines.size()==6 && lines[5].rfind(L"gui pid=",0)==0
                    && lines[5].find(L" datadir="+drive.dataDir.wstring())!=std::wstring::npos,
                    "restarted GUI received exactly the bootstrap data directory");
                const auto guiPid=guiPidFromRecord(lines);
                check(guiPid && waitPidExit(guiPid,10000),"fixture GUI exits on its own");
                check(drive.coordinator.restart(after(500))!=CoordinationResult::Restarted
                    && recordLines(drive.recordPath).size()==lines.size(),
                    "restart is single-shot: a second call never launches another GUI");
            }
        }
        clearChainEnv();drive.activity.cancelUpdate();
        std::error_code ec;fs::remove_all(base,ec);
    }
    {
        // UAC cancellation and plain launch failure both fail closed: no
        // session, no credential file residue, never an exit commit.
        std::cout<<"coordinator installer launch failures"<<std::endl;
        const auto base=root/L"launchfail";makeInstallRoot(base,source);
        for(auto behavior:{LaunchError::Cancelled,LaunchError::Failed}) {
            LaunchCapture capture;capture.behavior=behavior;
            CoordinatorOptions options;options.requireElevatedPeer=false;
            options.launcher=[&](const std::wstring& installer,const std::wstring& restrictedSwitch) {
                return fixtureLaunch(capture,installer,restrictedSwitch); };
            Coordinator c;LaunchError error=LaunchError::None;
            check(!c.startInstaller({source.wstring(),base.wstring(),{}},nullptr,nullptr,options,&error)
                && error==behavior,
                behavior==LaunchError::Cancelled?"UAC cancellation fails closed with the cancelled mapping"
                    :"installer launch failure fails closed");
            check(!c.canCommitExit(),"failed launch never authorizes exit");
            const auto locator=capture.arguments.size()>15?capture.arguments.substr(15):std::wstring();
            check(locatorShape(locator) && !fs::exists(credentialPath(locator)),
                "failed launch leaves no credential file behind");
        }
        std::error_code ec;fs::remove_all(base,ec);
    }
    {
        // Engine failure: the peer reports Failed after the handshake; no
        // commit, no completion, no restart, and nothing is installed.
        std::cout<<"coordinator installer engine failure"<<std::endl;
        const auto base=root/L"enginefail";
        ChainDrive drive;
        drive.installRoot=base/L"fail-install";drive.recordPath=base/L"fail-record.txt";drive.dataDir=base/L"fail-data";
        makeInstallRoot(drive.installRoot,source);fs::create_directory(drive.dataDir);
        check(drive.activity.enter(drive.installRoot.wstring())==ActivityError::None
            && drive.activity.reserveUpdate()==ActivityError::None,"engine-failure install root reserved");
        drive.reserved=drive.activity.identity();
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_RECORD",drive.recordPath.c_str());
        SetEnvironmentVariableW(L"ZZLOGG_FIXTURE_ENGINE",L"prepare-fail");
        CoordinatorOptions options;options.requireElevatedPeer=false;
        options.launcher=[&](const std::wstring& installer,const std::wstring& restrictedSwitch) {
            return fixtureLaunch(drive.capture,installer,restrictedSwitch); };
        LaunchError error=LaunchError::Failed;
        check(drive.coordinator.startInstaller({source.wstring(),drive.installRoot.wstring(),drive.dataDir.wstring()},
                &drive.reserved,nullptr,options,&error) && error==LaunchError::None,"engine-failure chain launches");
        check(drive.coordinator.authenticate(after(3000)),"failing engine still completes Hello");
        check(drive.coordinator.awaitAppExit(after(3000))==CoordinationResult::Failed
            && !drive.coordinator.canCommitExit() && !drive.coordinator.commitExit(after(100)),
            "engine failure refuses the exit commit");
        check(drive.coordinator.finish(after(3000))==CoordinationResult::Failed,"finish fails closed on engine failure");
        check(drive.coordinator.restart(after(500))==CoordinationResult::Failed,"restart refused without a completed transaction");
        check(!recordHasGui(recordLines(drive.recordPath)),"failed engine never restarts anything");
        clearChainEnv();drive.activity.cancelUpdate();
        std::error_code ec;fs::remove_all(base,ec);
    }
    {
        // Restart confirmation: an early-death GUI (no endpoint ever) and a
        // live GUI without its directory-scoped endpoint both fail the
        // bounded confirmation; nothing is reported as restarted.
        std::cout<<"coordinator restart confirmation"<<std::endl;
        for(auto mode:{L"die",L"noendpoint"}) {
            const auto base=root/(std::wstring(L"restart-")+mode);
            ChainDrive drive;
            const auto chainResult=driveChainToComplete(drive,source,base,mode,mode);
            check(chainResult!=0,"restart-confirmation chain completes");
            if(chainResult!=1) {
                std::cout<<"elevated harness: restart confirmation covered by ManualRestartRequired semantics"<<std::endl;
            } else {
                check(drive.coordinator.restart(after(4000))==CoordinationResult::RestartFailed,
                    mode==std::wstring(L"die")?"early-death GUI fails startup confirmation"
                        :"GUI without the single-instance endpoint fails startup confirmation");
                const auto lines=recordLines(drive.recordPath);
                check(recordHasGui(lines),"failed confirmation still launched the candidate GUI");
                const auto guiPid=guiPidFromRecord(lines);
                check(guiPid && waitPidExit(guiPid,15000),"candidate GUI exits on its own");
            }
            clearChainEnv();drive.activity.cancelUpdate();
            std::error_code ec;fs::remove_all(base,ec);
        }
    }
    {
        // Registration recheck before restart: removing the marker after a
        // completed transaction must refuse the restart (mutation sentinel:
        // deleting the recheck turns this into an unauthorized Restarted).
        std::cout<<"coordinator restart registration recheck"<<std::endl;
        const auto base=root/L"recheck";
        ChainDrive drive;
        const auto chainResult=driveChainToComplete(drive,source,base,L"recheck",L"ok");
        check(chainResult!=0,"recheck chain completes");
        if(chainResult!=1) {
            std::cout<<"elevated harness: registration recheck covered by ManualRestartRequired semantics"<<std::endl;
        } else {
            check(fs::remove(drive.installRoot/L".zzlogg-install-root"),"marker really removed");
            check(drive.coordinator.restart(after(3000))==CoordinationResult::RestartFailed,
                "missing marker refuses the restart after a completed transaction");
            check(!recordHasGui(recordLines(drive.recordPath)),"refused restart never launches the GUI");
        }
        clearChainEnv();drive.activity.cancelUpdate();
        std::error_code ec;fs::remove_all(base,ec);
    }
    {
        // Credential file contract: current-user-only protected DACL, round
        // trip, stale coordinator identity and locator/transaction mismatch
        // rejected; the engine fixture rejects a missing file and any token
        // on its command line.
        std::cout<<"coordinator credential file"<<std::endl;
        ProcessIdentity self;check(self.open(GetCurrentProcessId()),"self identity for credential tests");
        CredentialData data{};
        check(randomBytes(data.transaction.data(),16) && randomBytes(data.token.data(),32),"credential material generated");
        data.coordinatorPid=self.stamp().pid;data.coordinatorCreated=self.stamp().created;
        const auto locator=credentialLocator(data.transaction);
        check(locatorShape(locator),"locator is 16 lowercase hex nonzero");
        check(writeCredentialFile(data),"credential file written");
        bool protectedDacl=false;const auto aces=fileDacl(credentialPath(locator),protectedDacl);
        check(protectedDacl && aces.size()==1 && aces[0].sid==currentUserSid(),
            "credential file DACL is protected and current-user only");
        CredentialData round{};
        check(readCredentialFile(locator,round) && round.transaction==data.transaction && round.token==data.token,
            "credential round trip validates");
        check(deleteCredentialFile(locator) && !fs::exists(credentialPath(locator)),"credential file deleted");
        auto stale=data;stale.coordinatorCreated^=1;
        check(writeCredentialFile(stale),"stale-identity credential written");
        CredentialData junk{};
        check(!readCredentialFile(locator,junk),"stale coordinator identity rejected");
        check(deleteCredentialFile(locator),"stale credential cleaned");
        check(writeCredentialFile(data),"mismatch fixture credential written");
        const std::wstring otherLocator=L"00000000000000ab";
        check(locator!=otherLocator,"mismatch locator differs");
        std::error_code renameEc;fs::rename(credentialPath(locator),credentialPath(otherLocator),renameEc);
        check(!renameEc,"credential renamed under a foreign locator");
        check(!readCredentialFile(otherLocator,junk),"locator/transaction mismatch rejected");
        check(deleteCredentialFile(otherLocator),"mismatch fixture cleaned");
        check(!readCredentialFile(L"00000000000000cd",junk),"missing credential file rejected");
        const auto fixtureExe=fs::path(argv[1]).make_preferred();
        check(spawnArgs(fixtureExe,L"--tx-engine --install \"\" --staging \"\" --txroot \"\" --txid 00000000000000ab --version 2")==41,
            "engine fixture rejects a missing credential file");
        check(spawnArgs(fixtureExe,L"--tx-engine --install \"\" --staging \"\" --txroot \"\" --txid 00000000000000ab --version 2 fedcba9876543210")==2,
            "engine fixture rejects anything beyond the restricted argv");
    }
    std::error_code ec;fs::remove(source,ec);fs::remove(root/L"install",ec);fs::remove(root/L"runtime",ec);
    fs::remove(root/L"data",ec);fs::remove(root/L"proceed-record.txt",ec);fs::remove(root/L"violation-record.txt",ec);fs::remove(root,ec);
    std::cout<<"coordinator test complete"<<std::endl;return failures?1:0;
}
int wmain(int argc,wchar_t** argv) {
    try {return runCoordinatorTest(argc,argv);}
    catch(const std::exception& error) {
        std::cerr<<"FAIL: coordinator test exception: "<<error.what()<<std::endl;
        return 1;
    }
}
