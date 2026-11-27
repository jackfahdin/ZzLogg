#include "startupcheck_win.h"
#include "startupreadiness_win.h"
#include <vector>

namespace zzlogg::updater::detail {
std::wstring quoteWindowsArgument(const std::wstring& value) {
    std::wstring quoted=L"\"";
    std::size_t backslashes=0;
    for(const auto character:value) {
        if(character==L'\\') {++backslashes;continue;}
        quoted.append(backslashes*(character==L'"'?2:1),L'\\');
        backslashes=0;
        if(character==L'"')quoted+=L'\\';
        quoted+=character;
    }
    quoted.append(backslashes*2,L'\\');
    return quoted+L'"';
}
StartupProbeResult runStartupProbe(const std::wstring& installRoot,DWORD timeoutMs) {
    StartupProbeResult result;
    const auto failed=[&](const wchar_t* detail,DWORD error) {
        result.error=error;
        result.detail=std::wstring(detail)+L" (Win32 "+std::to_wstring(error)
            +L", exit "+std::to_wstring(result.exitCode)+L")";
        return result;
    };
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE rawRead=nullptr,rawWrite=nullptr;
    if(!CreatePipe(&rawRead,&rawWrite,&security,4096))return failed(L"probe output pipe",GetLastError());
    Handle read(rawRead),write(rawWrite);
    if(!SetHandleInformation(read.get(),HANDLE_FLAG_INHERIT,0))return failed(L"probe pipe protection",GetLastError());
    Handle null(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        &security,OPEN_EXISTING,0,nullptr));
    if(!null)return failed(L"probe standard handles",GetLastError());
    // Inherit only the two standard handles, never transaction credentials,
    // leases, journal handles or unrelated handles from the elevated engine.
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> storage(size);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&size))return failed(L"probe attributes",GetLastError());
    const HANDLE inherited[]{write.get(),null.get()};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inherited),sizeof(inherited),nullptr,nullptr)) {
        const auto error=GetLastError();DeleteProcThreadAttributeList(attributes);
        return failed(L"probe handle list",error);
    }
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=null.get();startup.StartupInfo.hStdError=null.get();
    startup.StartupInfo.hStdOutput=write.get();startup.lpAttributeList=attributes;
    PROCESS_INFORMATION child{};
    const auto image=installRoot+L"\\ZzLogg.exe";
    auto command=quoteWindowsArgument(image)+L" --update-probe";
    const auto launched=CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,installRoot.c_str(),&startup.StartupInfo,&child);
    const auto launchError=launched?ERROR_SUCCESS:GetLastError();
    DeleteProcThreadAttributeList(attributes);write.reset();null.reset();
    if(!launched)return failed(L"probe launch failed",launchError);
    Handle process(child.hProcess),thread(child.hThread);
    const auto wait=WaitForSingleObject(process.get(),timeoutMs);
    if(wait!=WAIT_OBJECT_0) {
        const auto error=wait==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError();
        TerminateProcess(process.get(),1);
        result.processExited=WaitForSingleObject(process.get(),5000)==WAIT_OBJECT_0;
        return failed(result.processExited?L"probe timed out or wait failed":L"probe still running; recovery required",error);
    }
    if(!GetExitCodeProcess(process.get(),&result.exitCode))return failed(L"probe exit code unavailable",GetLastError());
    if(result.exitCode!=0)return failed(L"probe failed",ERROR_SUCCESS);
    char output[128]{};DWORD available=0,received=0;
    // Bounded read after exit: a descendant holding an inherited pipe can
    // never leave the engine waiting for EOF indefinitely.
    if(!PeekNamedPipe(read.get(),nullptr,0,nullptr,&available,nullptr)
        || available==0 || available>=sizeof(output)
        || !ReadFile(read.get(),output,available,&received,nullptr))
        return failed(L"probe confirmation missing",ERROR_INVALID_DATA);
    const std::string marker(output,received);
    if(marker!="ZZLOGG_UPDATE_PROBE_OK\n" && marker!="ZZLOGG_UPDATE_PROBE_OK\r\n")
        return failed(L"probe confirmation invalid",ERROR_INVALID_DATA);
    result.passed=true;return result;
}
bool confirmApplicationStarted(HANDLE process,const DirectoryIdentity& identity,Deadline deadline) {
    const auto endpoint=singleInstancePipeName(identity,L"ZzLogg.exe");
    if(!process || endpoint.empty())return false;
    const auto readyName=startupReadyEventName(GetProcessId(process));
    while(remaining(deadline)) {
        if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
        Handle ready(OpenEventW(SYNCHRONIZE,FALSE,readyName.c_str()));
        if(ready && WaitForSingleObject(ready.get(),0)==WAIT_OBJECT_0
            && WaitNamedPipeW(endpoint.c_str(),20)!=FALSE
            && WaitForSingleObject(process,0)==WAIT_TIMEOUT)return true;
        Sleep(20);
    }
    return false;
}
}
