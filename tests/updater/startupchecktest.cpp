#include "startupcheck_win.h"
#include "updatertesthelpers.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[1])==L"--application") {
        SetEnvironmentVariableW(L"QT_QPA_PLATFORM",L"windows");
        const auto result=runStartupProbe(fs::path(argv[2]).parent_path().wstring());
        if(!result.passed)std::wcerr<<result.detail<<L'\n';
        return result.passed?0:1;
    }
    if(argc==3 && std::wstring(argv[1])==L"--application-readiness") {
        const fs::path app(argv[2]);
        const auto data=createTestRoot(fs::path(testTempDirectory()));
        auto command=quoteWindowsArgument(app.wstring())+L" --data-dir "+quoteWindowsArgument(data.wstring());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        if(!CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
            app.parent_path().c_str(),&startup,&child))return 1;
        Handle process(child.hProcess),thread(child.hThread);
        zzlogg::updater::DirectoryIdentity identity{};
        const bool identified=zzlogg::updater::InstallationActivity::probeIdentity(app.parent_path().wstring(),identity);
        const bool ready=identified && confirmApplicationStarted(process.get(),identity,after(20000));
        // Only this candidate is terminated, and only its isolated data is removed.
        TerminateProcess(process.get(),0);
        const bool exited=WaitForSingleObject(process.get(),5000)==WAIT_OBJECT_0;
        if(exited) {std::error_code ignored;fs::remove_all(data,ignored);}
        if(!ready)std::cerr<<"Primary application never published startup readiness\n";
        return ready && exited?0:1;
    }
    // The same image is a real child, with failure selected by a fixture file.
    if(argc==2 && std::wstring(argv[1])==L"--update-probe") {
        std::string mode;std::ifstream("mode")>>mode;
        if(mode=="hang") {Sleep(30000);return 0;}
        if(mode=="crash") return 7;
        if(mode=="silent") return 0;
        std::cout<<"ZZLOGG_UPDATE_PROBE_OK\n";return 0;
    }
    if(argc==2 && std::wstring(argv[1])==L"--early-endpoint") {
        zzlogg::updater::DirectoryIdentity identity{};
        zzlogg::updater::InstallationActivity::probeIdentity(fs::current_path().wstring(),identity);
        const auto name=zzlogg::updater::singleInstancePipeName(identity,L"ZzLogg.exe");
        Handle pipe(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_WAIT,
            1,64,64,0,nullptr));
        Sleep(1000);return 7; // real storage/UI startup failed after IPC creation
    }
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    const auto base=createTestRoot(fs::path(testTempDirectory()));
    const auto root=base/L"startup check";
    fs::create_directories(root);fs::copy_file(image,root/L"ZzLogg.exe");
    int failures=0;
    for(const auto* mode:{"ok","crash","silent","hang"}) {
        std::ofstream(root/L"mode")<<mode;
        const auto result=runStartupProbe(root.wstring(),std::string(mode)=="hang"?300:5000);
        if(result.passed!=(std::string(mode)=="ok") || !result.processExited) {
            std::cerr<<"Unexpected probe result: "<<mode<<'\n';++failures;
        }
        if(std::string(mode)=="crash" && result.exitCode!=7) ++failures;
        if(std::string(mode)=="hang" && result.error!=WAIT_TIMEOUT) ++failures;
    }
    // Neither an immediate exit nor an early single-instance pipe proves
    // the real storage/UI initialization finished.
    for(const auto* argument:{L"--update-probe",L"--early-endpoint"}) {
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        auto command=quoteWindowsArgument((root/L"ZzLogg.exe").wstring())+L" "+argument;
        std::ofstream(root/L"mode")<<"silent";
        if(!CreateProcessW((root/L"ZzLogg.exe").c_str(),command.data(),nullptr,nullptr,FALSE,0,
                           nullptr,root.c_str(),&startup,&child)) ++failures;
        else {
            zzlogg::updater::DirectoryIdentity identity{};
            zzlogg::updater::InstallationActivity::probeIdentity(root.wstring(),identity);
            if(confirmApplicationStarted(child.hProcess,identity,after(2000))) ++failures;
            WaitForSingleObject(child.hProcess,5000);CloseHandle(child.hThread);CloseHandle(child.hProcess);
        }
    }
    fs::remove_all(base);
    return failures?1:0;
}
