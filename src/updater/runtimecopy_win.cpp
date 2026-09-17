#include "runtimecopy_win_p.h"
#include "stablepackage_p.h"
#include "sha256_win_p.h"
#include <array>
#include <algorithm>
#include <winternl.h>
namespace zzlogg::updater::detail {
Handle createExclusiveDirectory(HANDLE parent,const std::wstring& leaf,PSECURITY_DESCRIPTOR security){
    // RootDirectory is already leased; only a single ordinary component may be
    // resolved relative to it. FILE_CREATE atomically creates AND returns a
    // non-share-delete handle, eliminating CreateDirectoryW/open replacement gaps.
    if(!security || leaf.empty() || leaf.size()>80 || !std::all_of(leaf.begin(),leaf.end(),[](wchar_t c){
        return (c>=L'a' && c<=L'z') || (c>=L'A' && c<=L'Z') || (c>=L'0' && c<=L'9') || c==L'-';
    }))return Handle{};
    BY_HANDLE_FILE_INFORMATION parentInfo{};
    if(!GetFileInformationByHandle(parent,&parentInfo) || !(parentInfo.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        || (parentInfo.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return Handle{};
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto create=module?reinterpret_cast<decltype(&NtCreateFile)>(GetProcAddress(module,"NtCreateFile")):nullptr;
    if(!create)return Handle{};
    UNICODE_STRING name{};name.Buffer=const_cast<PWSTR>(leaf.data());
    name.Length=static_cast<USHORT>(leaf.size()*sizeof(wchar_t));name.MaximumLength=name.Length;
    OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent;
    attributes.ObjectName=&name;attributes.Attributes=OBJ_CASE_INSENSITIVE;
    attributes.SecurityDescriptor=security;
    IO_STATUS_BLOCK status{};HANDLE raw=nullptr;
    constexpr ULONG createOnly=2,created=2,directoryFile=1,synchronousNonAlert=0x20,openReparsePoint=0x00200000;
    const auto result=create(&raw,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,&attributes,&status,nullptr,
        FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ,createOnly,directoryFile|synchronousNonAlert|openReparsePoint,nullptr,0);
    Handle directory(raw);
    if(result<0 || status.Information!=created)return Handle{};
    return directory;
}
Handle createExclusiveDirectory(HANDLE parent,const std::wstring& leaf){
    SecurityAttributes security;if(!security.get())return Handle{};
    return createExclusiveDirectory(parent,leaf,security.get()->lpSecurityDescriptor);
}
namespace {
using update::detail::StablePackage;
bool same(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow;
}
// Pin each ancestor before creating anything. No junction traversal or remote paths.
bool pinDirectories(const std::wstring& path,std::vector<Handle>& leases) {
    if(path.size()<3 || path.size()>=MAX_PATH-60 || path[1]!=L':' || path[2]!=L'\\' || GetDriveTypeW(path.substr(0,3).c_str())!=DRIVE_FIXED)return false;
    for(std::size_t pos=2;;) {
        auto current=path.substr(0,pos==2?3:pos);
        Handle h(CreateFileW(current.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        BY_HANDLE_FILE_INFORMATION info{};wchar_t finalPath[32768]{};
        auto len=h?GetFinalPathNameByHandleW(h.get(),finalPath,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS):0;
        const auto expected=L"\\\\?\\"+current;
        if(!h || !GetFileInformationByHandle(h.get(),&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
            || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !len || len>=32768
            || CompareStringOrdinal(finalPath,static_cast<int>(len),expected.c_str(),-1,TRUE)!=CSTR_EQUAL)return false;
        leases.push_back(std::move(h));
        if(pos==path.size())break;
        pos=path.find(L'\\',pos+1);if(pos==std::wstring::npos)pos=path.size();
    }
    return true;
}
void eraseExact(const std::wstring& path,const BY_HANDLE_FILE_INFORMATION& identity,bool directory) {
    Handle h(CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    BY_HANDLE_FILE_INFORMATION now{};
    if(h && GetFileInformationByHandle(h.get(),&now) && same(now,identity) && !(now.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        FILE_DISPOSITION_INFO disposition{TRUE};SetFileInformationByHandle(h.get(),FileDispositionInfo,&disposition,sizeof(disposition));
    }
}
}
struct RuntimeCopy::Impl {
    std::vector<Handle> ancestors;
    Handle directoryLease;
    StablePackage copy;
    std::wstring file,directory;
    BY_HANDLE_FILE_INFORMATION fileId{},directoryId{};
    bool fileOwned=false,directoryOwned=false;
    ~Impl() {
        copy=StablePackage{};
        if(fileOwned)eraseExact(file,fileId,false);
        directoryLease.reset();
        if(directoryOwned)eraseExact(directory,directoryId,true);
    }
};
RuntimeCopy::RuntimeCopy()=default;
RuntimeCopy::~RuntimeCopy()=default;
RuntimeCopy::RuntimeCopy(RuntimeCopy&&) noexcept=default;
RuntimeCopy& RuntimeCopy::operator=(RuntimeCopy&&) noexcept=default;
bool RuntimeCopy::create(const std::wstring& source,const std::wstring& base) {
    if(impl_)return false;
    StablePackage original;if(original.open(source)!=update::PackageVerificationError::None)return false;
    auto result=std::make_unique<Impl>();
    if(!pinDirectories(base,result->ancestors))return false;
    std::array<BYTE,16> nonce{};if(!randomBytes(nonce.data(),static_cast<ULONG>(nonce.size())))return false;
    const wchar_t hex[]=L"0123456789abcdef";
    result->directory=base+L"\\ZzLogg-update-";for(auto b:nonce){result->directory+=hex[b>>4];result->directory+=hex[b&15];}
    SecurityAttributes security;if(!security.get())return false;
    result->directoryLease=createExclusiveDirectory(result->ancestors.back().get(),result->directory.substr(base.size()+1));
    if(!result->directoryLease || !GetFileInformationByHandle(result->directoryLease.get(),&result->directoryId)
        || (result->directoryId.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    result->directoryOwned=true;result->file=result->directory+L"\\ZzLoggUpdate.exe";
    Handle destination(CreateFileW(result->file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,security.get(),CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!destination || !GetFileInformationByHandle(destination.get(),&result->fileId))return false;
    result->fileOwned=true;
    LARGE_INTEGER size{};if(!GetFileSizeEx(original.handle(),&size) || size.QuadPart<=0 || size.QuadPart>512LL*1024*1024)return false;
    update::detail::Sha256 hash;std::array<BYTE,65536> bytes{};uint64_t total=0;
    for(;;){DWORD read=0,written=0;
        if(!ReadFile(original.handle(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))return false;
        if(!read)break;total+=read;
        if(total>static_cast<uint64_t>(size.QuadPart) || !hash.add(bytes.data(),read)
            || !WriteFile(destination.get(),bytes.data(),read,&written,nullptr) || written!=read)return false;
    }
    std::array<uint8_t,32> digest{};if(total!=static_cast<uint64_t>(size.QuadPart) || !hash.finish(digest) || !FlushFileBuffers(destination.get()))return false;
    std::string sha;for(auto b:digest){sha+=static_cast<char>(hex[b>>4]);sha+=static_cast<char>(hex[b&15]);}
    // Retain identity across the writer-to-reader transition with no delete share.
    // The final read-only lease and rehash must succeed after closing the writer;
    // any intervening write or surviving write mapping prevents verification.
    Handle bridge(CreateFileW(result->file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!bridge)return false;destination.reset();
    if(result->copy.open(result->file)!=update::PackageVerificationError::None
        || result->copy.verifyContent(total,sha)!=update::PackageVerificationError::None
        || !original.identityUnchanged() || !result->copy.identityUnchanged())return false;
    // Original source lease is deliberately released here, before execution.
    impl_=std::move(result);return true;
}
bool RuntimeCopy::unchanged() const { return impl_ && impl_->copy.identityUnchanged(); }
const std::wstring& RuntimeCopy::path() const { static const std::wstring empty;return impl_?impl_->file:empty; }
const std::wstring& RuntimeCopy::directory() const { static const std::wstring empty;return impl_?impl_->directory:empty; }
}
