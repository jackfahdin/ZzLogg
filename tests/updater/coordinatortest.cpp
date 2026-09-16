#include "coordinator_p.h"
#include <filesystem>
#include <iostream>
#include <fstream>
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv) {
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
    auto root=fs::path(temp)/(L"ZzLogg-coordinate-test-"+std::to_wstring(GetCurrentProcessId()));fs::create_directory(root);
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
    std::error_code ec;fs::remove(source,ec);fs::remove(root/L"install",ec);fs::remove(root/L"runtime",ec);fs::remove(root,ec);
    std::cout<<"coordinator test complete"<<std::endl;return failures?1:0;
}
