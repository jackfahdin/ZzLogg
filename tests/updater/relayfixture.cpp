#include "bootstrap_win_p.h"
#include "relay_p.h"
#include <string>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
namespace {
// Injected installer launcher: models ShellExecuteEx runas by launching the
// fixture installer as an ordinary child and adopting its real identity.
LauncherOutcome launchFixtureInstaller(const std::wstring& installer,const std::wstring& restrictedSwitch) {
    LauncherOutcome outcome;
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
    outcome.process=std::move(identity);outcome.error=LaunchError::None;return outcome;
}
}
// Test stand-in entry point: the relay logic is the very same relay.cpp, only
// the launcher is replaced by the fixture. Production main.cpp never links
// this file.
int wmain(int argc,wchar_t** argv){
    ChildBootstrap bootstrap;
    if(!bootstrap.open(argc,argv))return BootstrapRejected;
    CoordinatorOptions options;
    options.requireElevatedPeer=false;
    options.launcher=[](const std::wstring& installer,const std::wstring& restrictedSwitch){
        return launchFixtureInstaller(installer,restrictedSwitch);
    };
    return runInstallRelay(bootstrap,options);
}
