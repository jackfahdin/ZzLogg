#include "coordinator_p.h"
#include "installationactivity_win.h"
#include "installlock_win.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <array>
#include <stdexcept>
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
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
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
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",mode);Coordinator coordinator;
        check(coordinator.start(source.wstring(),(root/L"runtime").wstring()),"fixture starts through real runtime copy");
        if(!coordinator.process().handle())continue;
        check(!coordinator.canCommitExit(),"process creation never authorizes exit");
        wchar_t image[32768]{};DWORD length=32768;
        check(QueryFullProcessImageNameW(coordinator.process().handle(),0,image,&length) && fs::path(image)==fs::path(coordinator.runtimePath()),"actual child executes new runtime path");
        const auto authenticated=coordinator.authenticate(after(250));
        std::wstring m=mode;
        if(m==L"early" || m==L"timeout" || m==L"token" || m==L"order") {
            check(!authenticated && !coordinator.canCommitExit(),"bad or absent Hello refuses exit");continue;
        }
        check(authenticated && !coordinator.canCommitExit(),"Hello authenticates but cannot exit");
        auto waiting=coordinator.awaitAppExit(after(200));
        if(m!=L"success" && m!=L"linger") {
            check(waiting!=CoordinationResult::WaitingForAppExit && !coordinator.canCommitExit(),"replay cancel ready-only and early death refuse exit");continue;
        }
        check(waiting==CoordinationResult::WaitingForAppExit && coordinator.canCommitExit(),"only live authenticated AwaitingAppExit authorizes exit");
        check(MoveFileW(source.c_str(),(root/L"install"/L"old.exe").c_str()),"original file renames while runtime process is alive");
        fs::copy_file(argv[1],source);fs::remove(root/L"install"/L"old.exe");
        check(coordinator.commitExit(after(500)),"commit sent only after guard");
        if(m==L"linger")check(coordinator.finish(after(50))==CoordinationResult::PeerRunning,"Complete message cannot substitute actual process exit");
        auto completed=coordinator.finish(after(2000));ProcessIdentity self;self.open(GetCurrentProcessId());
        check(completed==(self.elevated()?CoordinationResult::ManualRestartRequired:CoordinationResult::Complete),"final exit observed and elevation restart policy applied");
    }
    SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",nullptr);
    {
        std::cout<<"coordinator deferred cleanup"<<std::endl;
        SetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",L"linger");ULONGLONG beforeDestruction=0;std::wstring path;ProcessIdentity observer;
        {Coordinator c;check(c.start(fs::path(argv[1]).make_preferred().wstring(),root.wstring()) && c.authenticate(after(2000))
            && c.awaitAppExit(after(1000))==CoordinationResult::WaitingForAppExit && c.commitExit(after(500)),"lingering ownership fixture starts");
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
    std::error_code ec;fs::remove(source,ec);fs::remove(root/L"install",ec);fs::remove(root/L"runtime",ec);fs::remove(root,ec);
    std::cout<<"coordinator test complete"<<std::endl;return failures?1:0;
}
int wmain(int argc,wchar_t** argv) {
    try {return runCoordinatorTest(argc,argv);}
    catch(const std::exception& error) {
        std::cerr<<"FAIL: coordinator test exception: "<<error.what()<<std::endl;
        return 1;
    }
}
