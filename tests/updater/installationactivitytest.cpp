#include "installationactivity_win.h"
#include "installlock_win.h"
#include "updatertesthelpers.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <type_traits>
using namespace zzlogg::updater;
namespace fs=std::filesystem;
namespace {
int failures=0;
void check(bool value,const char* name) { if(!value) { ++failures; std::cerr<<"FAIL: "<<name<<" (win32 "<<GetLastError()<<")\n"; } }
struct Handle {
    HANDLE value=nullptr;
    ~Handle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Child {
    PROCESS_INFORMATION process{};
    bool start(const std::wstring& arguments,bool inherit=false) {
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
        std::wstring command=L"\""+std::wstring(executable)+L"\" "+arguments;
        STARTUPINFOW startup{}; startup.cb=sizeof(startup);
        return CreateProcessW(executable,command.data(),nullptr,nullptr,inherit,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
    }
    DWORD finish() {
        if(!process.hProcess) return 999;
        if(WaitForSingleObject(process.hProcess,10000)!=WAIT_OBJECT_0) { TerminateProcess(process.hProcess,998); return 998; }
        DWORD code=999; GetExitCodeProcess(process.hProcess,&code); return code;
    }
    ~Child() { if(process.hThread) CloseHandle(process.hThread); if(process.hProcess) CloseHandle(process.hProcess); }
};
DWORD probeEnter(const std::wstring& path) { Child child; if(!child.start(L"--probe-enter \""+path+L"\"")) return 999; return child.finish(); }
DWORD probeReserve(const std::wstring& path) { Child child; if(!child.start(L"--probe-reserve \""+path+L"\"")) return 999; return child.finish(); }
// Real behavioral proof of the sharing rules this coordination relies on. The
// design must never be inferred from flag documentation alone.
void verifySharingSemantics(const std::wstring& path) {
    const auto open=[&](DWORD access,DWORD share) {
        HANDLE h=CreateFileW(path.c_str(),access,share,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        return Handle{h==INVALID_HANDLE_VALUE?nullptr:h};
    };
    {
        auto lease=open(FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ);
        check(lease.value!=nullptr,"semantics: updater-style lease opens");
        auto probe=open(FILE_LIST_DIRECTORY,0);
        check(lease.value && !probe.value && GetLastError()==ERROR_SHARING_VIOLATION,
            "semantics: read-share updater lease blocks the exclusive quiescence probe");
    }
    {
        auto lease=open(FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE);
        check(lease.value!=nullptr,"semantics: activity lease opens");
        auto probe=open(FILE_LIST_DIRECTORY,0);
        check(lease.value && !probe.value && GetLastError()==ERROR_SHARING_VIOLATION,
            "semantics: read/write-share activity lease blocks the exclusive quiescence probe");
        // Regression guard: the long-lived activity lease must not turn the
        // installation directory read-only (locator/settings writes next to a
        // portable executable keep working).
        const auto filePath=(fs::path(path)/L"zz-activity-write-probe.tmp");
        Handle file{CreateFileW(filePath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        check(lease.value && file.value!=nullptr,
            "semantics: file creation inside the leased directory stays possible");
        CloseHandle(file.value); file.value=nullptr;
        check(DeleteFileW(filePath.c_str())!=FALSE,"semantics: write probe cleaned up");
        check(lease.value && !MoveFileExW(path.c_str(),(fs::path(path).wstring()+L"-moved").c_str(),0),
            "semantics: activity lease still pins the directory name");
    }
    {
        auto pin=open(0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
        check(pin.value!=nullptr,"semantics: zero-access identity pin opens");
        auto probe=open(FILE_LIST_DIRECTORY,0);
        check(pin.value && probe.value,
            "semantics: zero-access identity pin stays outside sharing checks");
        FILE_ID_INFO a{},b{};
        check(pin.value && probe.value
            && GetFileInformationByHandleEx(pin.value,FileIdInfo,&a,sizeof(a))
            && GetFileInformationByHandleEx(probe.value,FileIdInfo,&b,sizeof(b))
            && a.VolumeSerialNumber==b.VolumeSerialNumber
            && !std::memcmp(a.FileId.Identifier,b.FileId.Identifier,16),
            "semantics: identity pin observes the same directory identity");
    }
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc>=3 && std::wstring(argv[1])==L"--probe-enter") {
        InstallationActivity activity;
        return activity.enter(argv[2])==ActivityError::None?0:10;
    }
    if(argc>=3 && std::wstring(argv[1])==L"--probe-reserve") {
        InstallationActivity activity;
        if(activity.enter(argv[2])!=ActivityError::None) return 10;
        return activity.reserveUpdate()==ActivityError::None?0:30;
    }
    if(argc==5 && (std::wstring(argv[1])==L"--hold-enter" || std::wstring(argv[1])==L"--hold-reserve")) {
        InstallationActivity activity;
        if(activity.enter(argv[2])!=ActivityError::None) return 20;
        if(std::wstring(argv[1])==L"--hold-reserve" && activity.reserveUpdate()!=ActivityError::None) return 30;
        SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[3])));
        WaitForSingleObject(reinterpret_cast<HANDLE>(std::stoull(argv[4])),10000);
        return 0;
    }
    if(argc==4 && std::wstring(argv[1])==L"--crash-reserve") {
        InstallationActivity activity;
        if(activity.enter(argv[2])!=ActivityError::None) return 20;
        if(activity.reserveUpdate()!=ActivityError::None) return 30;
        SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[3])));
        ExitProcess(0); // Deliberately bypass RAII to exercise a crashed reservation.
    }
    static_assert(!std::is_copy_constructible_v<InstallationActivity>
        && std::is_nothrow_move_constructible_v<InstallationActivity>);
    const auto temp=detail::testTempDirectory();
    const auto root=fs::path(temp)/(L"ZzLogg-activity-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(root/L"install"); fs::create_directories(root/L"other");
    const auto path=(root/L"install").wstring();
    const auto other=(root/L"other").wstring();
    verifySharingSemantics(path);
    std::wstring updateMutexName;
    {
        InstallationActivity naming;
        check(naming.enter(path)==ActivityError::None,"identity probe instance enters");
        DirectoryIdentity identity=naming.identity();
        check(identity.volumeSerial!=0
            && std::any_of(identity.fileId.begin(),identity.fileId.end(),[](auto b){return b!=0;}),
            "entered identity carries volume and file ID");
        updateMutexName=InstallLock::mutexName(identity);
        check(!updateMutexName.empty(),"update mutex name derived from directory identity");
        auto alias=path; std::transform(alias.begin(),alias.end(),alias.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});
        DirectoryIdentity aliasIdentity{};
        check(InstallationActivity::probeIdentity(alias,aliasIdentity)
            && aliasIdentity.volumeSerial==identity.volumeSerial && aliasIdentity.fileId==identity.fileId,
            "case alias resolves to identical directory identity");
        DirectoryIdentity missing{};
        check(!InstallationActivity::probeIdentity(path+L"\\missing",missing),"missing directory has no identity");
    }
    {
        InstallationActivity first,second;
        check(first.enter(path)==ActivityError::None && first.entered(),"first instance enters");
        check(second.enter(path)==ActivityError::None && second.entered(),"second same-directory instance enters");
        check(first.enter(path)!=ActivityError::None,"re-entering is not a second lease");
        check(first.identityUnchanged() && second.identityUnchanged(),"entered identities verified");
        check(first.reserveUpdate()==ActivityError::Blocked,"same-directory instance blocks reservation");
        check(!first.updateReserved(),"blocked reservation is not held");
        second=InstallationActivity{};
        check(first.reserveUpdate()==ActivityError::None && first.updateReserved(),"reservation succeeds once alone");
        check(first.identityUnchanged(),"identity stable across reservation");
        check(first.reserveUpdate()==ActivityError::Blocked,"double reservation rejected");
        check(probeEnter(path)==10,"real child cannot start during reservation");
        check(probeEnter(other)==0,"real child in sibling directory unaffected");
        check(probeReserve(other)==0,"sibling directory reserves independently");
        {
            InstallLock updater;
            check(updater.acquire(path)==InstallLockError::Blocked,
                "3B.3 install lock observes the reservation on the same directory identity");
        }
        check(!MoveFileExW(path.c_str(),(root/L"moved").c_str(),0),"reserved installation cannot be replaced");
        check(!MoveFileExW(root.c_str(),fs::path(root.wstring()+L"-moved").c_str(),0),"reserved ancestors cannot be replaced");
        InstallationActivity movedActivity(std::move(first));
        check(!first.updateReserved() && movedActivity.updateReserved() && probeEnter(path)==10,
            "move preserves the reservation");
        movedActivity.cancelUpdate();
        check(!movedActivity.updateReserved() && movedActivity.entered(),"cancellation keeps the activity lease");
        check(probeEnter(path)==0,"real child starts again after cancellation");
        check(movedActivity.reserveUpdate()==ActivityError::None,"reservation repeatable after cancellation");
        movedActivity.cancelUpdate();
    }
    check(probeEnter(path)==0,"lease fully released after destruction");
    {
        // Real multi-process coordination: a live child blocks this process.
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        Handle ready{CreateEventW(&attributes,TRUE,FALSE,nullptr)},go{CreateEventW(&attributes,TRUE,FALSE,nullptr)};
        Child child;
        check(child.start(L"--hold-enter \""+path+L"\" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.value))
            +L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(go.value)),true),"activity child starts");
        check(WaitForSingleObject(ready.value,10000)==WAIT_OBJECT_0,"child holds activity lease");
        InstallationActivity local;
        check(local.enter(path)==ActivityError::None,"multiple real instances coexist");
        check(local.reserveUpdate()==ActivityError::Blocked,"live real child blocks reservation");
        check(probeReserve(path)==30,"real child reservation blocked by two active instances");
        SetEvent(go.value); check(child.finish()==0,"child releases cleanly");
        check(local.reserveUpdate()==ActivityError::None,"reservation after real child exit");
        local.cancelUpdate();
    }
    {
        // Crash release: with no observer the reservation dies with the process.
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        Handle ready{CreateEventW(&attributes,TRUE,FALSE,nullptr)};
        Child child;
        check(child.start(L"--crash-reserve \""+path+L"\" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.value)),true),
            "crashing child starts");
        check(WaitForSingleObject(ready.value,10000)==WAIT_OBJECT_0,"crashing child reserved before death");
        check(child.finish()==0,"crashing child exited without destructors");
        check(probeEnter(path)==0,"crashed reservation releases once no handle survives");
    }
    {
        // Crash release with an observer: the existing object stays fail-closed
        // until the last handle is gone (this is the task3 handoff foundation).
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        Handle ready{CreateEventW(&attributes,TRUE,FALSE,nullptr)};
        Child child;
        check(child.start(L"--crash-reserve \""+path+L"\" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.value)),true),
            "observed crashing child starts");
        check(WaitForSingleObject(ready.value,10000)==WAIT_OBJECT_0,"observed child reserved before death");
        Handle observer{OpenMutexW(SYNCHRONIZE,FALSE,updateMutexName.c_str())};
        check(observer.value!=nullptr,"observer retains the reservation object");
        check(child.finish()==0,"observed child exited without destructors");
        check(probeEnter(path)==10,"observed crashed reservation stays fail-closed");
        CloseHandle(observer.value); observer.value=nullptr;
        check(probeEnter(path)==0,"reservation releases after the last observer handle");
    }
    {
        // A held 3B.3 install lock (the updater lease) blocks new application entries.
        InstallLock updater;
        check(updater.acquire(path)==InstallLockError::None,"updater lease acquired");
        InstallationActivity activity;
        check(activity.enter(path)==ActivityError::Blocked,"updater lease blocks application entry");
        check(probeEnter(path)==10,"real child entry blocked by updater lease");
    }
    {
        InstallationActivity activity;
        check(activity.enter(path)==ActivityError::None,"entry for ancestor protection");
        check(!MoveFileExW(path.c_str(),(root/L"moved").c_str(),0),"active installation cannot be replaced");
        check(!MoveFileExW(root.c_str(),fs::path(root.wstring()+L"-moved").c_str(),0),"active ancestors cannot be replaced");
    }
    check(MoveFileExW(path.c_str(),(root/L"moved").c_str(),0)!=FALSE,"directory replaceable once activity ends");
    check(MoveFileExW((root/L"moved").c_str(),path.c_str(),0)!=FALSE,"directory restored");
    for(const auto& invalid:{std::wstring{},std::wstring(L"relative"),std::wstring(L"C:\\"),
        std::wstring(L"\\\\localhost\\C$\\Windows"),path+L"\\.."}) {
        InstallationActivity activity;
        check(activity.enter(invalid)==ActivityError::InvalidRoot && !activity.entered(),
            "unsupported path form rejected without blocking startup semantics");
        check(activity.reserveUpdate()==ActivityError::Unavailable,"reservation requires entry");
    }
    {
        InstallationActivity activity;
        check(activity.enter(path+L"\\missing")==ActivityError::Unavailable,"missing directory unavailable");
        Handle file{CreateFileW((root/L"plain-file").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        check(activity.enter((root/L"plain-file").wstring())==ActivityError::Unavailable,"file is not an installation");
    }
    {
        // Preplanted unowned mutex on the directory identity stays fail-closed for entry.
        InstallationActivity naming;
        check(naming.enter(path)==ActivityError::None,"naming instance enters");
        const auto name=InstallLock::mutexName(naming.identity());
        Handle preempted{CreateMutexW(nullptr,FALSE,name.c_str())};
        check(preempted.value!=nullptr,"preemption fixture created");
        InstallationActivity blocked;
        check(blocked.enter(other)==ActivityError::None,"other directory still enters while planted");
        check(probeEnter(path)==10,"real child entry blocked by preexisting mutex");
        InstallationActivity victim;
        check(victim.enter(path)==ActivityError::Blocked,"preexisting mutex blocks entry even while leased");
    }
    std::error_code error; fs::remove_all(root,error);
    check(!error,"temporary fixtures removed");
    std::cout<<"installation activity failures: "<<failures<<'\n';
    return failures?1:0;
}
