#include "installlock_win.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <algorithm>
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
DWORD probe(const std::wstring& path) { Child child; if(!child.start(L"--probe \""+path+L"\"")) return 999; return child.finish(); }
void checkMutexAccess(const std::wstring& name) {
    // A default/per-user DACL mutation must fail: this real object's ACL needs
    // authenticated-local wait/release access and an explicit network denial.
    Handle mutex{OpenMutexW(READ_CONTROL,FALSE,name.c_str())};
    PACL acl=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    const auto result=GetSecurityInfo(mutex.value,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,
        nullptr,nullptr,&acl,nullptr,&descriptor);
    bool localAllowed=false,networkDenied=false;
    if(result==ERROR_SUCCESS && acl) for(DWORD i=0;i<acl->AceCount;++i) {
        void* entry=nullptr;
        if(!GetAce(acl,i,&entry)) continue;
        const auto* header=static_cast<ACE_HEADER*>(entry);
        if(header->AceType==ACCESS_ALLOWED_ACE_TYPE) {
            auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
            if(IsWellKnownSid(&ace->SidStart,WinAuthenticatedUserSid))
                localAllowed=ace->Mask==(SYNCHRONIZE|MUTEX_MODIFY_STATE);
        }
        if(header->AceType==ACCESS_DENIED_ACE_TYPE) {
            auto* ace=static_cast<ACCESS_DENIED_ACE*>(entry);
            if(IsWellKnownSid(&ace->SidStart,WinNetworkSid))
                networkDenied=(ace->Mask&(SYNCHRONIZE|MUTEX_MODIFY_STATE))==(SYNCHRONIZE|MUTEX_MODIFY_STATE);
        }
    }
    check(result==ERROR_SUCCESS && localAllowed && networkDenied,"real mutex ACL permits authenticated local users and denies network");
    if(descriptor) LocalFree(descriptor);
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc>=3 && std::wstring(argv[1])==L"--probe") {
        InstallLock lock;
        return lock.acquire(argv[2])==InstallLockError::None?0:10;
    }
    if(argc==5 && std::wstring(argv[1])==L"--abandon") {
        InstallLock lock;
        if(lock.acquire(argv[2])!=InstallLockError::None) return 20;
        SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[3])));
        WaitForSingleObject(reinterpret_cast<HANDLE>(std::stoull(argv[4])),10000);
        ExitProcess(0); // Deliberately bypass RAII to exercise a genuinely abandoned mutex.
    }
    static_assert(!std::is_copy_constructible_v<InstallLock> && std::is_nothrow_move_constructible_v<InstallLock>);
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
    const auto root=fs::path(temp)/(L"ZzLogg-lock-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(root/L"install"); fs::create_directories(root/L"other");
    const auto path=(root/L"install").wstring();
    DirectoryIdentity identity{};
    {
        InstallLock first;
        check(first.acquire(path)==InstallLockError::None && first.ownsLock() && first.identityUnchanged(),"valid directory leased");
        if(!first.ownsLock()) { std::error_code ec; fs::remove_all(root,ec); return 1; }
        identity=first.identity();
        checkMutexAccess(InstallLock::mutexName(identity));
        check(identity.volumeSerial!=0 && std::any_of(identity.fileId.begin(),identity.fileId.end(),[](auto b){return b!=0;}),"volume and file ID captured");
        check(probe(path)==10,"real child same-directory contention blocked");
        InstallLock local;
        check(local.acquire(path)!=InstallLockError::None,"same-thread recursion is not a second lease");
        check(probe((root/L"other").wstring())==0,"real child other directory independent");
        auto alias=path; std::transform(alias.begin(),alias.end(),alias.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});
        check(probe(alias)==10,"case alias contends on same identity");
        check(!MoveFileExW(path.c_str(),(root/L"moved").c_str(),0),"held installation cannot be replaced");
        check(!MoveFileExW(root.c_str(),fs::path(root.wstring()+L"-moved").c_str(),0),"held ancestor cannot be replaced");
        InstallLock moved(std::move(first));
        check(!first.ownsLock() && moved.identityUnchanged() && probe(path)==10,"move preserves exclusive lease");
        InstallLock assigned; assigned=std::move(moved);
        check(!moved.ownsLock() && assigned.ownsLock() && probe(path)==10,"move assignment preserves exclusive lease");
    }
    check(probe(path)==0,"real child obtains lease after clean release");
    {
        auto alias=path; std::transform(alias.begin(),alias.end(),alias.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});
        InstallLock lock; check(lock.acquire(alias)==InstallLockError::None,"case alias accepted");
        check(InstallLock::mutexName(lock.identity())==InstallLock::mutexName(identity),"case aliases have identical object identity");
    }
    for(const auto& invalid:{std::wstring{},std::wstring(L"relative"),std::wstring(L"C:\\"),std::wstring(L"\\\\localhost\\C$\\Windows"),
        std::wstring(L"\\\\?\\C:\\Windows"),path+L"\\..",path+L"\\.",path+L" ",path+L"\\missing"}) {
        InstallLock lock; check(lock.acquire(invalid)!=InstallLockError::None && !lock.ownsLock(),"invalid/network/root/missing path rejected");
    }
    {
        Handle file{CreateFileW((root/L"plain-file").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        InstallLock lock; check(lock.acquire((root/L"plain-file").wstring())!=InstallLockError::None,"file is not installation directory");
    }
    // Removing OPEN_REPARSE_POINT/ancestor checks must fail these real junction tests.
    fs::create_directories(root/L"junction");
    const auto target=L"\\??\\"+(root/L"other").wstring();
    const auto print=(root/L"other").wstring();
    struct Junction { DWORD tag; WORD length,reserved,subOffset,subLength,printOffset,printLength; wchar_t names[1024]; } junction{};
    junction.tag=IO_REPARSE_TAG_MOUNT_POINT;
    junction.subLength=static_cast<WORD>(target.size()*sizeof(wchar_t));
    junction.printOffset=junction.subLength+sizeof(wchar_t);
    junction.printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
    std::copy(target.begin(),target.end(),junction.names);
    std::copy(print.begin(),print.end(),junction.names+target.size()+1);
    junction.length=static_cast<WORD>(8+junction.printOffset+junction.printLength+sizeof(wchar_t));
    bool junctionCreated=false;
    {
        Handle handle{CreateFileW((root/L"junction").c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr)};
        DWORD bytes=0;
        junctionCreated=DeviceIoControl(handle.value,FSCTL_SET_REPARSE_POINT,&junction,junction.length+8,nullptr,0,&bytes,nullptr)!=FALSE;
    }
    check(junctionCreated,"junction fixture created");
    if(junctionCreated) {
        fs::create_directories(root/L"other"/L"child");
        InstallLock direct,ancestor;
        check(direct.acquire((root/L"junction").wstring())!=InstallLockError::None,"reparse installation rejected");
        check(ancestor.acquire((root/L"junction"/L"child").wstring())!=InstallLockError::None,"reparse ancestor rejected");
        RemoveDirectoryW((root/L"junction").c_str());
    }
    const auto name=InstallLock::mutexName(identity);
    {
        Handle preempted{CreateMutexW(nullptr,FALSE,name.c_str())};
        check(preempted.value!=nullptr,"preemption fixture created");
        check(probe(path)==10,"preexisting unowned mutex blocks");
    }
    {
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;GA;;;WD)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"denied ACL fixture created");
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        Handle denied{CreateMutexW(&attributes,FALSE,name.c_str())};
        LocalFree(descriptor);
        check(denied.value!=nullptr && probe(path)==10,"access denied mutex blocks");
    }
    {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        Handle ready{CreateEventW(&attributes,TRUE,FALSE,nullptr)},go{CreateEventW(&attributes,TRUE,FALSE,nullptr)};
        Child child;
        check(child.start(L"--abandon \""+path+L"\" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.value))
            +L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(go.value)),true),"abandoning child starts");
        const bool signaled=WaitForSingleObject(ready.value,10000)==WAIT_OBJECT_0;
        check(signaled,"child holds lease before death");
        Handle observer{signaled?OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,name.c_str()):nullptr};
        check(observer.value!=nullptr,"independent observer retains mutex");
        SetEvent(go.value); check(child.finish()==0,"holder exits without destructors");
        InstallLock lock;
        check(lock.acquire(path)==InstallLockError::Abandoned && !lock.ownsLock(),"observed abandoned mutex blocks");
        check(probe(path)==10,"consumed abandonment stays blocked while object exists");
    }
    check(probe(path)==0,"destroyed object is not a durable crash journal");
    std::error_code error; fs::remove_all(root,error);
    check(!error,"temporary fixtures removed");
    std::cout<<"install lock failures: "<<failures<<'\n';
    return failures?1:0;
}
