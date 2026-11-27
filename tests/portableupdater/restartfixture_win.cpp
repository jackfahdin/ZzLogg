#include "startupreadiness_win.h"
#include "startupcheck_win.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
int wmain(int argc,wchar_t** argv) {
    if(argc==2 && std::wstring(argv[1])==L"--update-probe") {
        std::cout<<"ZZLOGG_UPDATE_PROBE_OK\n";return 0;
    }
    if(argc!=3 || std::wstring(argv[1])!=L"--data-dir")return 2;
    std::ofstream("restarted")<<std::filesystem::path(argv[2]).u8string();
    DirectoryIdentity identity{};
    if(!InstallationActivity::probeIdentity(std::filesystem::current_path().wstring(),identity))return 3;
    const auto name=singleInstancePipeName(identity,L"ZzLogg.exe");
    Handle pipe(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_WAIT,1,256,256,0,nullptr));
    if(!pipe)return 4;
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    OVERLAPPED overlapped{};overlapped.hEvent=event.get();
    ConnectNamedPipe(pipe.get(),&overlapped);
    const auto readyName=startupReadyEventName(GetCurrentProcessId());
    Handle ready(CreateEventW(nullptr,TRUE,TRUE,readyName.c_str()));
    Sleep(500);
    CancelIoEx(pipe.get(),&overlapped);
    DWORD transferred=0;GetOverlappedResult(pipe.get(),&overlapped,&transferred,TRUE);
    return 0;
}
