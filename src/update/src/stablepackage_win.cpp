#include "stablepackage_p.h"
#include "sha256_win_p.h"
#include <algorithm>
#include <utility>

namespace zzlogg::update::detail {
namespace {
class Handle {
public:
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):value_(value) {}
    ~Handle() { if(value_!=INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(Handle&& other) noexcept:value_(std::exchange(other.value_,INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other) {
            if(value_!=INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_=std::exchange(other.value_,INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    Handle(const Handle&)=delete;
    HANDLE get() const { return value_; }
private:
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
bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh
        && a.nFileIndexLow==b.nFileIndexLow;
}
bool matchesPath(HANDLE handle,const std::wstring& path) {
    std::array<wchar_t,32768> buffer{};
    const auto length=GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),
                                              FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto expected=L"\\\\?\\"+path;
    return length && length<buffer.size() && CompareStringOrdinal(buffer.data(),static_cast<int>(length),
        expected.data(),static_cast<int>(expected.size()),TRUE)==CSTR_EQUAL;
}
bool ordinary(HANDLE handle,bool directory,BY_HANDLE_FILE_INFORMATION& info) {
    return GetFileType(handle)==FILE_TYPE_DISK && GetFileInformationByHandle(handle,&info)
        && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        && static_cast<bool>(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory
        && (directory || info.nNumberOfLinks==1);
}
struct Node {
    Handle handle;
    std::wstring path;
    BY_HANDLE_FILE_INFORMATION identity{};
};
}
struct StablePackage::Impl {
    std::vector<Node> directories;
    Node file;
};
StablePackage::StablePackage()=default;
StablePackage::~StablePackage()=default;
StablePackage::StablePackage(StablePackage&&) noexcept=default;
StablePackage& StablePackage::operator=(StablePackage&&) noexcept=default;
PackageVerificationError StablePackage::open(const std::wstring& path) {
    impl_.reset();
    if(!standardPath(path)) return PackageVerificationError::InvalidPath;
    if(GetDriveTypeW(path.substr(0,3).c_str())!=DRIVE_FIXED) return PackageVerificationError::InvalidPath;
    auto lease=std::make_unique<Impl>();
    for(std::size_t end=2;;end=path.find(L'\\',end+1)) {
        if(end==std::wstring::npos) break;
        auto parent=path.substr(0,end==2?3:end);
        Handle directory(CreateFileW(parent.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if(directory.get()==INVALID_HANDLE_VALUE || !ordinary(directory.get(),true,info)
            || !matchesPath(directory.get(),parent)) return PackageVerificationError::FileUnavailable;
        lease->directories.push_back({std::move(directory),std::move(parent),info});
    }
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(file.get()==INVALID_HANDLE_VALUE || !ordinary(file.get(),false,info)
        || !matchesPath(file.get(),path)) return PackageVerificationError::FileUnavailable;
    // All directory handles remain live: children cannot be removed to turn an
    // ancestor into a junction, and writable/deletable opens of the file conflict.
    lease->file=Node{std::move(file),path,info};
    impl_=std::move(lease);
    if(!identityUnchanged()) { impl_.reset(); return PackageVerificationError::FileUnavailable; }
    return PackageVerificationError::None;
}
PackageVerificationError StablePackage::verifyContent(std::uint64_t expectedSize,const std::string& expectedHash) {
    if(!impl_) return PackageVerificationError::FileUnavailable;
    if(expectedSize==0 || expectedSize>512ULL*1024*1024) return PackageVerificationError::SizeMismatch;
    if(expectedHash.size()!=64 || !std::all_of(expectedHash.begin(),expectedHash.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f'); })) return PackageVerificationError::HashMismatch;
    LARGE_INTEGER size{},start{};
    if(!GetFileSizeEx(handle(),&size)) return PackageVerificationError::FileUnavailable;
    if(size.QuadPart<0 || static_cast<std::uint64_t>(size.QuadPart)!=expectedSize) return PackageVerificationError::SizeMismatch;
    if(!SetFilePointerEx(handle(),start,nullptr,FILE_BEGIN)) return PackageVerificationError::FileUnavailable;
    Sha256 hash;
    std::array<BYTE,64*1024> buffer{};
    std::uint64_t total=0;
    for(;;) {
        DWORD read=0;
        if(!ReadFile(handle(),buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)) return PackageVerificationError::FileUnavailable;
        if(!read) break;
        total+=read;
        if(total>expectedSize) return PackageVerificationError::SizeMismatch;
        if(!hash.add(buffer.data(),read)) return PackageVerificationError::FileUnavailable;
    }
    if(total!=expectedSize) return PackageVerificationError::SizeMismatch;
    std::array<std::uint8_t,32> digest{};
    if(!hash.finish(digest)) return PackageVerificationError::FileUnavailable;
    constexpr char hex[]="0123456789abcdef";
    std::string actual;
    for(auto byte:digest) { actual+=hex[byte>>4]; actual+=hex[byte&15]; }
    return actual==expectedHash?PackageVerificationError::None:PackageVerificationError::HashMismatch;
}
bool StablePackage::identityUnchanged() const {
    if(!impl_) return false;
    const auto check=[](const Node& node,bool directory) {
        BY_HANDLE_FILE_INFORMATION current{},reopened{};
        if(!ordinary(node.handle.get(),directory,current) || !sameIdentity(node.identity,current)
            || !matchesPath(node.handle.get(),node.path)) return false;
        Handle pathHandle(CreateFileW(node.path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        return pathHandle.get()!=INVALID_HANDLE_VALUE && ordinary(pathHandle.get(),directory,reopened)
            && sameIdentity(current,reopened);
    };
    for(const auto& node:impl_->directories) if(!check(node,true)) return false;
    return check(impl_->file,false);
}
HANDLE StablePackage::handle() const { return impl_?impl_->file.handle.get():INVALID_HANDLE_VALUE; }
const std::wstring& StablePackage::path() const { static const std::wstring empty; return impl_?impl_->file.path:empty; }
}
