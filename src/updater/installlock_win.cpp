#include "installlock_win.h"
#include "installlock_win_p.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>
namespace zzlogg::updater {
namespace detail {
bool standardInstallationPath(const std::wstring& path) {
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
bool sameDirectoryIdentity(const DirectoryIdentity& a,const DirectoryIdentity& b) {
    return a.volumeSerial==b.volumeSerial && a.fileId==b.fileId;
}
bool directoryIdentity(HANDLE handle,DirectoryIdentity& identity) {
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
bool directoryMatchesPath(HANDLE handle,const std::wstring& path) {
    std::array<wchar_t,32768> buffer{};
    const auto length=GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto expected=L"\\\\?\\"+path;
    return length && length<buffer.size() && CompareStringOrdinal(buffer.data(),static_cast<int>(length),
        expected.data(),static_cast<int>(expected.size()),TRUE)==CSTR_EQUAL;
}
Handle openLeasedDirectory(const std::wstring& path) {
    return Handle(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
}
MutexClaim claimDirectoryMutex(const std::wstring& name,Handle& out) {
    out.reset();
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    // Network logons are denied; authenticated local users get exactly wait and
    // release rights. No caller obtains authority to modify an installation.
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(D;;GA;;;NU)(A;;0x00100001;;;AU)",SDDL_REVISION_1,&descriptor,nullptr))
        return MutexClaim::Error;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    Handle mutex(CreateMutexExW(&attributes,name.c_str(),CREATE_MUTEX_INITIAL_OWNER,SYNCHRONIZE|MUTEX_MODIFY_STATE));
    const auto error=GetLastError();
    LocalFree(descriptor);
    if(!mutex) return MutexClaim::Blocked;
    if(error==ERROR_ALREADY_EXISTS) {
        const auto wait=WaitForSingleObject(mutex.get(),0);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) ReleaseMutex(mutex.get());
        // Even an unowned preexisting object could have been planted. Never
        // treat it, or a consumed abandoned state, as permission to proceed.
        return wait==WAIT_ABANDONED?MutexClaim::Abandoned:MutexClaim::Blocked;
    }
    out=std::move(mutex);
    return MutexClaim::Owned;
}
}
namespace {
struct Node { detail::Handle handle; std::wstring path; DirectoryIdentity identity; };
}
struct InstallLock::Impl {
    std::vector<Node> directories;
    detail::Handle mutex;
    bool owned=false;
    ~Impl() { if(owned) ReleaseMutex(mutex.get()); }
    bool unchanged() const {
        for(const auto& node:directories) {
            DirectoryIdentity current{},reopened{};
            auto check=detail::openLeasedDirectory(node.path);
            if(!detail::directoryIdentity(node.handle.get(),current) || !detail::sameDirectoryIdentity(current,node.identity)
                || !detail::directoryMatchesPath(node.handle.get(),node.path) || !check
                || !detail::directoryIdentity(check.get(),reopened) || !detail::sameDirectoryIdentity(current,reopened)) return false;
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
    if(!detail::standardInstallationPath(directory) || GetDriveTypeW(directory.substr(0,3).c_str())!=DRIVE_FIXED)
        return InstallLockError::InvalidRoot;
    auto lease=std::make_unique<Impl>();
    for(std::size_t end=2;;) {
        auto path=directory.substr(0,end==2?3:end);
        auto handle=detail::openLeasedDirectory(path);
        DirectoryIdentity identity{};
        if(!handle || !detail::directoryIdentity(handle.get(),identity) || !detail::directoryMatchesPath(handle.get(),path))
            return InstallLockError::Unavailable;
        lease->directories.push_back({std::move(handle),std::move(path),identity});
        if(end==directory.size()) break;
        end=directory.find(L'\\',end+1);
        if(end==std::wstring::npos) end=directory.size();
    }
    const auto name=mutexName(lease->directories.back().identity);
    if(name.empty()) return InstallLockError::Unavailable;
    detail::Handle mutex;
    const auto claim=detail::claimDirectoryMutex(name,mutex);
    if(claim==detail::MutexClaim::Error) return InstallLockError::Unavailable;
    if(claim==detail::MutexClaim::Blocked) return InstallLockError::Blocked;
    if(claim==detail::MutexClaim::Abandoned) return InstallLockError::Abandoned;
    lease->mutex=std::move(mutex);
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
