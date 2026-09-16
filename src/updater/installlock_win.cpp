#include "installlock_win.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>
namespace zzlogg::updater {
namespace {
class Handle {
public:
    explicit Handle(HANDLE value=nullptr):value_(value) {}
    ~Handle() { close(); }
    Handle(Handle&& other) noexcept:value_(std::exchange(other.value_,nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other) { close(); value_=std::exchange(other.value_,nullptr); }
        return *this;
    }
    Handle(const Handle&)=delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_!=INVALID_HANDLE_VALUE; }
private:
    void close() { if(*this) CloseHandle(value_); }
    HANDLE value_;
};
bool standardPath(const std::wstring& path) {
    if(path.size()<4 || path.size()>=MAX_PATH || path[1]!=L':' || path[2]!=L'\\'
        || !((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z'))) return false;
    for(std::size_t begin=3;begin<path.size();) {
        const auto end=path.find(L'\\',begin);
        const auto component=path.substr(begin,end==std::wstring::npos?end:end-begin);
        if(component.empty() || component.back()==L'.' || component.back()==L' ') return false;
        for(const auto c:component) if(c<32 || std::wstring_view(L"/:*?\"<>|").find(c)!=std::wstring_view::npos) return false;
        auto stem=component.substr(0,component.find(L'.'));
        for(auto& c:stem) if(c>=L'a' && c<=L'z') c-=L'a'-L'A';
        if(stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL"
            || stem==L"CONIN$" || stem==L"CONOUT$") return false;
        if(stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT")
            && ((stem[3]>=L'1' && stem[3]<=L'9') || stem[3]==L'\u00b9' || stem[3]==L'\u00b2' || stem[3]==L'\u00b3')) return false;
        if(end==std::wstring::npos) return true;
        begin=end+1;
    }
    return false;
}
bool sameIdentity(const DirectoryIdentity& a,const DirectoryIdentity& b) {
    return a.volumeSerial==b.volumeSerial && a.fileId==b.fileId;
}
bool directoryInfo(HANDLE handle,DirectoryIdentity& identity) {
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    FILE_ID_INFO info{};
    if(GetFileType(handle)!=FILE_TYPE_DISK
        || !GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&attributes,sizeof(attributes))
        || !(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        || (attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        || !GetFileInformationByHandleEx(handle,FileIdInfo,&info,sizeof(info))) return false;
    identity.volumeSerial=info.VolumeSerialNumber;
    std::copy_n(info.FileId.Identifier,identity.fileId.size(),identity.fileId.begin());
    return true;
}
bool matchesPath(HANDLE handle,const std::wstring& path) {
    std::array<wchar_t,32768> buffer{};
    const auto length=GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto expected=L"\\\\?\\"+path;
    return length && length<buffer.size() && CompareStringOrdinal(buffer.data(),static_cast<int>(length),
        expected.data(),static_cast<int>(expected.size()),TRUE)==CSTR_EQUAL;
}
Handle openDirectory(const std::wstring& path) {
    // Deny write/delete sharing: keeps every ancestor from becoming a junction
    // or being renamed while still allowing ordinary file activity beneath it.
    // Attribute-only opens do not participate in all sharing checks. Request
    // directory read access as well so denial of delete sharing pins the name.
    return Handle(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
}
struct Node { Handle handle; std::wstring path; DirectoryIdentity identity; };
}
struct InstallLock::Impl {
    std::vector<Node> directories;
    Handle mutex;
    bool owned=false;
    ~Impl() { if(owned) ReleaseMutex(mutex.get()); }
    bool unchanged() const {
        for(const auto& node:directories) {
            DirectoryIdentity current{},reopened{};
            auto check=openDirectory(node.path);
            if(!directoryInfo(node.handle.get(),current) || !sameIdentity(current,node.identity)
                || !matchesPath(node.handle.get(),node.path) || !check
                || !directoryInfo(check.get(),reopened) || !sameIdentity(current,reopened)) return false;
        }
        return !directories.empty();
    }
};
InstallLock::InstallLock()=default;
InstallLock::~InstallLock()=default;
InstallLock::InstallLock(InstallLock&&) noexcept=default;
InstallLock& InstallLock::operator=(InstallLock&&) noexcept=default;
InstallLockError InstallLock::acquire(const std::wstring& directory) {
    // Do not silently drop an existing lease when a caller attempts reacquisition.
    if(impl_) return InstallLockError::Blocked;
    if(!standardPath(directory) || GetDriveTypeW(directory.substr(0,3).c_str())!=DRIVE_FIXED)
        return InstallLockError::InvalidRoot;
    auto lease=std::make_unique<Impl>();
    for(std::size_t end=2;;) {
        auto path=directory.substr(0,end==2?3:end);
        auto handle=openDirectory(path);
        DirectoryIdentity identity{};
        if(!handle || !directoryInfo(handle.get(),identity) || !matchesPath(handle.get(),path))
            return InstallLockError::Unavailable;
        lease->directories.push_back({std::move(handle),std::move(path),identity});
        if(end==directory.size()) break;
        end=directory.find(L'\\',end+1);
        if(end==std::wstring::npos) end=directory.size();
    }
    const auto name=mutexName(lease->directories.back().identity);
    if(name.empty()) return InstallLockError::Unavailable;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    // Network logons are denied; authenticated local users get exactly wait and
    // release rights. No caller obtains authority to modify an installation.
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(D;;GA;;;NU)(A;;0x00100001;;;AU)",SDDL_REVISION_1,&descriptor,nullptr))
        return InstallLockError::Unavailable;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    lease->mutex=Handle(CreateMutexExW(&attributes,name.c_str(),CREATE_MUTEX_INITIAL_OWNER,SYNCHRONIZE|MUTEX_MODIFY_STATE));
    const auto error=GetLastError();
    LocalFree(descriptor);
    if(!lease->mutex) return InstallLockError::Blocked;
    if(error==ERROR_ALREADY_EXISTS) {
        const auto wait=WaitForSingleObject(lease->mutex.get(),0);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) ReleaseMutex(lease->mutex.get());
        // Even an unowned preexisting object could have been planted. Never
        // treat it, or a consumed abandoned state, as permission to proceed.
        return wait==WAIT_ABANDONED?InstallLockError::Abandoned:InstallLockError::Blocked;
    }
    lease->owned=true;
    if(!lease->unchanged()) return InstallLockError::Unavailable;
    impl_=std::move(lease);
    return InstallLockError::None;
}
bool InstallLock::ownsLock() const { return impl_ && impl_->owned; }
bool InstallLock::identityUnchanged() const { return ownsLock() && impl_->unchanged(); }
DirectoryIdentity InstallLock::identity() const { return impl_?impl_->directories.back().identity:DirectoryIdentity{}; }
std::wstring InstallLock::mutexName(const DirectoryIdentity& identity) {
    // Explicit encoding avoids padding/ABI differences across callers.
    std::array<UCHAR,24> input{};
    for(std::size_t i=0;i<8;++i) input[i]=static_cast<UCHAR>(identity.volumeSerial>>(i*8));
    std::copy(identity.fileId.begin(),identity.fileId.end(),input.begin()+8);
    std::array<UCHAR,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,input.data(),static_cast<ULONG>(input.size()),
        digest.data(),static_cast<ULONG>(digest.size()))<0) return {};
    std::wstring name=L"Global\\ZzLogg.InstallLock.v1.";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto byte:digest) { name+=hex[byte>>4]; name+=hex[byte&15]; }
    return name;
}
}
