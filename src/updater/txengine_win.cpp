#include "txengine_win.h"
#include "installlock_win_p.h"
#include "sha256_win_p.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <algorithm>
#include <map>
#include <set>
namespace zzlogg::updater {
namespace {
constexpr wchar_t kMarkerFile[]=L".zzlogg-install-root";
constexpr wchar_t kInstalledManifest[]=L".zzlogg-files.manifest";
constexpr wchar_t kStagedManifest[]=L"files.manifest";
constexpr BYTE kManifestMagic[8]={'Z','Z','T','X','M','A','N','1'};
std::wstring hexSeq(std::uint64_t value){
    const wchar_t hex[]=L"0123456789abcdef";std::wstring out;
    for(int i=15;i>=0;--i)out+=hex[(value>>(i*4))&15];
    return out;
}
std::wstring canonicalPath(const std::wstring& path){
    std::wstring full(32768,L'\0');
    const auto length=GetFullPathNameW(path.c_str(),static_cast<DWORD>(full.size()),full.data(),nullptr);
    if(!length || length>=full.size())return{};
    full.resize(length);
    while(full.size()>3 && full.back()==L'\\')full.pop_back();
    return full;
}
bool samePath(const std::wstring& a,const std::wstring& b){
    const auto ca=canonicalPath(a),cb=canonicalPath(b);
    return !ca.empty() && ca.size()==cb.size()
        && CompareStringOrdinal(ca.c_str(),static_cast<int>(ca.size()),cb.c_str(),static_cast<int>(cb.size()),TRUE)==CSTR_EQUAL;
}
// Strict UTF-8 decode: rejects overlong forms, surrogates, out-of-range code
// points and truncation. Control characters are rejected by the path rules.
bool decodeUtf8(const BYTE* at,std::size_t size,std::wstring& out){
    out.clear();
    for(std::size_t i=0;i<size;){
        const BYTE lead=at[i];
        if(lead<0x80){out+=static_cast<wchar_t>(lead);++i;continue;}
        int extra=0;char32_t cp=0,min=0;
        if(lead>=0xc2 && lead<=0xdf){extra=1;cp=lead&0x1f;min=0x80;}
        else if(lead>=0xe0 && lead<=0xef){extra=2;cp=lead&0x0f;min=0x800;}
        else if(lead>=0xf0 && lead<=0xf4){extra=3;cp=lead&0x07;min=0x10000;}
        else return false;
        if(i+extra>=size)return false;
        for(int k=1;k<=extra;++k){
            const BYTE next=at[i+k];
            if(next<0x80 || next>0xbf)return false;
            cp=(cp<<6)|(next&0x3f);
        }
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
        if(cp<0x10000)out+=static_cast<wchar_t>(cp);
        else{cp-=0x10000;out+=static_cast<wchar_t>(0xd800+(cp>>10));out+=static_cast<wchar_t>(0xdc00+(cp&0x3ff));}
        i+=extra+1;
    }
    return true;
}
void encodeUtf8(const std::wstring& wide,std::vector<BYTE>& out){
    for(std::size_t i=0;i<wide.size();++i){
        char32_t cp=wide[i];
        if(cp>=0xd800 && cp<=0xdbff && i+1<wide.size() && wide[i+1]>=0xdc00 && wide[i+1]<=0xdfff){
            cp=0x10000+((cp-0xd800)<<10)+(wide[++i]-0xdc00);
        }
        if(cp<0x80)out.push_back(static_cast<BYTE>(cp));
        else if(cp<0x800){out.push_back(static_cast<BYTE>(0xc0|(cp>>6)));out.push_back(static_cast<BYTE>(0x80|(cp&0x3f)));}
        else if(cp<0x10000){out.push_back(static_cast<BYTE>(0xe0|(cp>>12)));out.push_back(static_cast<BYTE>(0x80|((cp>>6)&0x3f)));out.push_back(static_cast<BYTE>(0x80|(cp&0x3f)));}
        else{out.push_back(static_cast<BYTE>(0xf0|(cp>>18)));out.push_back(static_cast<BYTE>(0x80|((cp>>12)&0x3f)));out.push_back(static_cast<BYTE>(0x80|((cp>>6)&0x3f)));out.push_back(static_cast<BYTE>(0x80|(cp&0x3f)));}
    }
}
bool validRelativePath(const std::wstring& path){
    if(path.empty() || path.size()>kMaxManifestPathBytes || path.front()==L'/' || path.back()==L'/')return false;
    for(std::size_t begin=0;;){
        const auto end=path.find(L'/',begin);
        const auto component=path.substr(begin,end==std::wstring::npos?end:end-begin);
        if(component.empty() || component==L"." || component==L".."
            || component.back()==L'.' || component.back()==L' ')return false;
        for(const auto c:component)if(c<32 || c==L'\\' || c==L':')return false;
        if(end==std::wstring::npos)return true;
        begin=end+1;
    }
}
std::wstring upperKey(const std::wstring& path){
    std::wstring upper=path;
    for(auto& c:upper)if(c>=L'a' && c<=L'z')c-=L'a'-L'A';
    return upper;
}
bool g32(const BYTE*& at,const BYTE* end,std::uint32_t& v){
    if(static_cast<std::size_t>(end-at)<4)return false;v=0;
    for(int i=0;i<4;++i)v|=std::uint32_t(at[i])<<(i*8);at+=4;return true;
}
bool g64(const BYTE*& at,const BYTE* end,std::uint64_t& v){
    if(static_cast<std::size_t>(end-at)<8)return false;v=0;
    for(int i=0;i<8;++i)v|=std::uint64_t(at[i])<<(i*8);at+=8;return true;
}
void w32(std::vector<BYTE>& out,std::uint32_t v){for(int i=0;i<4;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
void w64(std::vector<BYTE>& out,std::uint64_t v){for(int i=0;i<8;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
std::uint64_t saturated(std::uint64_t a,std::uint64_t b){return b>~0ull-a?~0ull:a+b;}
std::wstring targetPathFor(const std::wstring& root,const std::wstring& relpath){
    std::wstring native=relpath;
    std::replace(native.begin(),native.end(),L'/',L'\\');
    return root+L"\\"+native;
}
// Transaction pin for the installation root: write sharing stays open so the
// journaled child replace/delete/create operations can proceed, while delete
// sharing is denied so the directory can neither be renamed nor swapped for a
// junction mid-transaction. Identity is verified through the handle.
detail::Handle pinForTransaction(const std::wstring& root){
    detail::Handle pin(CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    DirectoryIdentity identity{};
    if(!pin || !detail::directoryIdentity(pin.get(),identity)
        || !detail::directoryMatchesPath(pin.get(),root))return detail::Handle{};
    return pin;
}
// Managed-file read open: reparse points and directories are refused and
// delete sharing is denied, so the bytes read belong to the named file.
detail::Handle openManagedRead(const std::wstring& path){
    detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file)return detail::Handle{};
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.get(),&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return detail::Handle{};
    return file;
}
// Streams the already-open source into a brand-new destination while hashing.
bool copyWithHash(HANDLE source,const std::wstring& destination,
    std::array<std::uint8_t,32>& digest,std::uint64_t& size){
    detail::Handle out(CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!out)return false;
    update::detail::Sha256 sha;std::vector<BYTE> chunk(1u<<20);size=0;bool ok=true;
    for(;;){
        DWORD got=0;
        if(!ReadFile(source,chunk.data(),static_cast<DWORD>(chunk.size()),&got,nullptr)){ok=false;break;}
        if(!got)break;
        DWORD written=0;
        if(!sha.add(chunk.data(),got) || !WriteFile(out.get(),chunk.data(),got,&written,nullptr) || written!=got){ok=false;break;}
        size+=got;
    }
    ok=ok && FlushFileBuffers(out.get())!=FALSE && sha.finish(digest);
    out.reset();
    if(!ok)DeleteFileW(destination.c_str());
    return ok;
}
// Copies the staged payload file into a fresh temp file, proving the bytes
// match the new manifest entry. Any mismatch refuses before placement.
bool stageToTemp(const std::wstring& staged,const TxManifestEntry& entry,const std::wstring& temp){
    auto source=openManagedRead(staged);
    if(!source)return false;
    std::array<std::uint8_t,32> digest{};std::uint64_t size=0;
    return copyWithHash(source.get(),temp,digest,size) && digest==entry.sha256 && size==entry.size;
}
bool ensureParentDirectories(const std::wstring& installRoot,const std::wstring& relpath){
    std::wstring current=installRoot;
    for(std::size_t begin=0;;){
        const auto end=relpath.find(L'/',begin);
        if(end==std::wstring::npos)return true; // leaf reached; parents exist
        current+=L'\\'+relpath.substr(begin,end-begin);
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){
            if(!CreateDirectoryW(current.c_str(),nullptr))return false;
        }else if(!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        begin=end+1;
    }
}
bool notReparseFile(const std::wstring& path){
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)
        && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
// Real 64-bit HKLM view. The key is re-validated at this boundary too, so the
// production boundary itself refuses any other key even if callers change.
class Win64Registry final:public TxRegistry {
    static constexpr const wchar_t* subkey(){
        return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg";
    }
    detail::Handle open(const std::wstring& key,REGSAM access){
        if(!allowedRegistryKey(key))return detail::Handle{};
        HKEY raw=nullptr;
        if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,subkey(),0,KEY_WOW64_64KEY|access,&raw)!=ERROR_SUCCESS)return detail::Handle{};
        return detail::Handle(raw);
    }
public:
    bool readString(const std::wstring& key,const std::wstring& name,std::wstring& value) override{
        auto handle=open(key,KEY_READ);if(!handle)return false;
        DWORD type=0,size=0;
        if(RegGetValueW(reinterpret_cast<HKEY>(handle.get()),nullptr,name.c_str(),RRF_RT_REG_SZ,&type,nullptr,&size)
            !=ERROR_SUCCESS || !size || size>(64u<<10))return false;
        std::wstring buffer(size/sizeof(wchar_t)+1,L'\0');
        if(RegGetValueW(reinterpret_cast<HKEY>(handle.get()),nullptr,name.c_str(),RRF_RT_REG_SZ,&type,
            buffer.data(),&size)!=ERROR_SUCCESS)return false;
        value.assign(buffer.c_str());return true;
    }
    bool readDword(const std::wstring& key,const std::wstring& name,std::uint32_t& value) override{
        auto handle=open(key,KEY_READ);if(!handle)return false;
        DWORD type=0,data=0,size=sizeof(data);
        if(RegGetValueW(reinterpret_cast<HKEY>(handle.get()),nullptr,name.c_str(),RRF_RT_REG_DWORD,&type,&data,&size)
            !=ERROR_SUCCESS)return false;
        value=data;return true;
    }
    bool writeString(const std::wstring& key,const std::wstring& name,const std::wstring& value) override{
        auto handle=open(key,KEY_SET_VALUE);if(!handle)return false;
        return RegSetValueExW(reinterpret_cast<HKEY>(handle.get()),name.c_str(),0,REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
    }
};
bool wellKnownSystemOrAdmin(PSID sid){
    BYTE systemBuffer[SECURITY_MAX_SID_SIZE]{},adminsBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD size=sizeof(systemBuffer);
    if(!CreateWellKnownSid(WinLocalSystemSid,nullptr,systemBuffer,&size))return false;
    if(EqualSid(sid,systemBuffer))return true;
    size=sizeof(adminsBuffer);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,adminsBuffer,&size))return false;
    return EqualSid(sid,adminsBuffer)!=FALSE;
}
}
bool allowedRegistryKey(const std::wstring& key){
    constexpr std::size_t length=std::wstring_view(kRegistrationKey).size();
    return key.size()==length
        && CompareStringOrdinal(key.c_str(),static_cast<int>(key.size()),
            kRegistrationKey,static_cast<int>(length),TRUE)==CSTR_EQUAL;
}
std::unique_ptr<TxRegistry> productionRegistry(){return std::make_unique<Win64Registry>();}
TxManifestError parseManifestFile(const std::wstring& path,std::vector<TxManifestEntry>& entries){
    entries.clear();
    detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file)return TxManifestError::Unreadable;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.get(),&size))return TxManifestError::Unreadable;
    if(size.QuadPart>static_cast<LONGLONG>(kMaxManifestBytes))return TxManifestError::TooLarge;
    if(size.QuadPart<16)return TxManifestError::BadFormat;
    std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
        return TxManifestError::Unreadable;
    const BYTE* at=bytes.data();const BYTE* const end=at+bytes.size();
    if(!std::equal(std::begin(kManifestMagic),std::end(kManifestMagic),at))return TxManifestError::BadFormat;
    at+=sizeof(kManifestMagic);
    std::uint32_t version=0,count=0;
    if(!g32(at,end,version) || version!=1 || !g32(at,end,count) || count>kMaxManifestEntries)
        return TxManifestError::BadFormat;
    std::vector<TxManifestEntry> parsed;std::set<std::wstring> seen;
    for(std::uint32_t i=0;i<count;++i){
        std::uint32_t pathBytes=0;TxManifestEntry entry{};
        if(!g32(at,end,pathBytes))return TxManifestError::BadFormat;
        if(pathBytes==0 || pathBytes>kMaxManifestPathBytes)return TxManifestError::UnsafePath;
        if(!g64(at,end,entry.size))return TxManifestError::BadFormat;
        if(end-at<32+static_cast<ptrdiff_t>(pathBytes))return TxManifestError::BadFormat;
        std::copy_n(at,32,entry.sha256.begin());at+=32;
        if(!decodeUtf8(at,pathBytes,entry.relpath) || !validRelativePath(entry.relpath))
            return TxManifestError::UnsafePath;
        at+=pathBytes;
        if(!seen.insert(upperKey(entry.relpath)).second)return TxManifestError::UnsafePath;
        parsed.push_back(std::move(entry));
    }
    if(at!=end)return TxManifestError::BadFormat;
    entries=std::move(parsed);
    return TxManifestError::None;
}
bool writeManifestFile(const std::wstring& path,const std::vector<TxManifestEntry>& entries){
    if(entries.size()>kMaxManifestEntries)return false;
    std::set<std::wstring> seen;
    std::vector<BYTE> bytes(std::begin(kManifestMagic),std::end(kManifestMagic));
    w32(bytes,1);w32(bytes,static_cast<std::uint32_t>(entries.size()));
    for(const auto& entry:entries){
        if(!validRelativePath(entry.relpath) || !seen.insert(upperKey(entry.relpath)).second)return false;
        std::vector<BYTE> utf8;encodeUtf8(entry.relpath,utf8);
        if(utf8.empty() || utf8.size()>kMaxManifestPathBytes)return false;
        w32(bytes,static_cast<std::uint32_t>(utf8.size()));w64(bytes,entry.size);
        bytes.insert(bytes.end(),entry.sha256.begin(),entry.sha256.end());
        bytes.insert(bytes.end(),utf8.begin(),utf8.end());
    }
    if(bytes.size()>kMaxManifestBytes)return false;
    detail::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)return false;
    DWORD written=0;
    return WriteFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)
        && written==bytes.size() && FlushFileBuffers(file.get())!=FALSE;
}
bool sha256FileContent(const std::wstring& path,std::array<std::uint8_t,32>& digest,std::uint64_t* size){
    auto file=openManagedRead(path);
    if(!file)return false;
    update::detail::Sha256 sha;std::vector<BYTE> chunk(1u<<20);std::uint64_t total=0;
    for(;;){
        DWORD got=0;
        if(!ReadFile(file.get(),chunk.data(),static_cast<DWORD>(chunk.size()),&got,nullptr))return false;
        if(!got)break;
        if(!sha.add(chunk.data(),got))return false;
        total+=got;
    }
    if(size)*size=total;
    return sha.finish(digest);
}
// The protected root must be owned by Administrators/SYSTEM, and no Allow ACE
// carrying a data-write bit may belong to any other principal.
bool protectedRootAclShape(const void* ownerSid,const void* daclArg){
    auto* const owner=static_cast<PSID>(const_cast<void*>(ownerSid));
    auto* const dacl=static_cast<PACL>(const_cast<void*>(daclArg));
    bool ok=owner && dacl && wellKnownSystemOrAdmin(owner);
    // Explicit data-write capabilities only. FILE_GENERIC_WRITE also carries
    // SYNCHRONIZE and READ_CONTROL, which are not write access and appear in
    // every read-only grant (icacls (R) = FILE_GENERIC_READ = 0x120089), so
    // the generic file mask would reject the installer's own hardened root.
    constexpr ACCESS_MASK writeBits=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA
        |FILE_WRITE_ATTRIBUTES|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    for(WORD i=0;ok && i<dacl->AceCount;++i){
        void* entry=nullptr;
        if(!GetAce(dacl,i,&entry)){ok=false;break;}
        const auto* header=static_cast<ACE_HEADER*>(entry);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE)continue;
        const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        if((ace->Mask&writeBits) && !wellKnownSystemOrAdmin(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart))))ok=false;
    }
    return ok;
}
// The engine executable must nest beneath the protected transaction root, and
// that root must be owned and writable exclusively by Administrators/SYSTEM.
bool productionProtectedImage(const std::wstring& image,const std::wstring& protectedRoot){
    if(!detail::standardInstallationPath(image) || !detail::standardInstallationPath(protectedRoot))return false;
    const auto root=canonicalPath(protectedRoot),module=canonicalPath(image);
    if(root.empty() || module.size()<=root.size()+1 || module[root.size()]!=L'\\'
        || CompareStringOrdinal(module.c_str(),static_cast<int>(root.size()),
            root.c_str(),static_cast<int>(root.size()),TRUE)!=CSTR_EQUAL)return false;
    PSID owner=nullptr;PACL dacl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(GetNamedSecurityInfoW(root.c_str(),SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&descriptor)!=ERROR_SUCCESS)
        return false;
    const bool ok=protectedRootAclShape(owner,dacl);
    if(descriptor)LocalFree(descriptor);
    return ok;
}
namespace {
struct PlannedFile {
    TxOperation op=TxOperation::Create;
    TxManifestEntry incoming{};   // new content for Create/Replace
    TxManifestEntry expectedOld{}; // old content for Replace/Delete
    std::wstring relpath,target,staged;
};
struct RollbackContext {
    TxRegistry* registry=nullptr;
    std::wstring tempDir;
    std::map<std::wstring,const TxJournalRecord*> backups; // target -> its Backup record
};
// Restores bytes from a backup file into the target through a fresh temp file,
// proving the restored content against the expected old digest.
bool restoreFromBackup(const TxJournalRecord& record,const std::wstring& tempDir,
    const std::array<std::uint8_t,32>& oldDigest,std::uint64_t oldSize){
    if(record.backup.empty() || !notReparseFile(record.backup))return false;
    const auto temp=tempDir+L"\\rest-"+hexSeq(record.seq)+L"-"+hexSeq(GetTickCount64());
    auto source=openManagedRead(record.backup);
    if(!source)return false;
    std::array<std::uint8_t,32> digest{};std::uint64_t size=0;
    if(!copyWithHash(source.get(),temp,digest,size) || digest!=oldDigest || size!=oldSize)return false;
    if(!MoveFileExW(temp.c_str(),record.target.c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temp.c_str());return false;
    }
    DeleteFileW(record.backup.c_str());
    return true;
}
// One reverse-order undo step. Every branch is idempotent: an already-undone
// operation is a zero op, a foreign or unexpected state refuses closed.
bool undoRecord(const TxJournalRecord& record,const RollbackContext& context){
    switch(record.op){
    case TxOperation::Complete:
        return true; // handled by the caller before undo starts
    case TxOperation::Backup:
        return true; // orphan backup: nothing was replaced yet; scene preserved
    case TxOperation::Registry:{
        if(!allowedRegistryKey(record.target) || !context.registry)return false;
        std::wstring current;
        if(!context.registry->readString(record.target,L"DisplayVersion",current))return false;
        if(current==record.oldRegistryValue)return true;
        return context.registry->writeString(record.target,L"DisplayVersion",record.oldRegistryValue);
    }
    case TxOperation::Create:{
        if(GetFileAttributesW(record.target.c_str())==INVALID_FILE_ATTRIBUTES)return true;
        std::array<std::uint8_t,32> digest{};
        if(!sha256FileContent(record.target,digest) || digest!=record.sha256)return false;
        return DeleteFileW(record.target.c_str())!=FALSE;
    }
    case TxOperation::Delete:{
        const auto attributes=GetFileAttributesW(record.target.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES){
            std::array<std::uint8_t,32> digest{};
            if(!sha256FileContent(record.target,digest) || digest!=record.sha256)return false;
            DeleteFileW(record.backup.c_str()); // restored earlier; drop the duplicate
            return true;
        }
        return restoreFromBackup(record,context.tempDir,record.sha256,record.size);
    }
    case TxOperation::Replace:{
        const auto backup=context.backups.find(record.target);
        if(backup==context.backups.end())return false;
        const auto attributes=GetFileAttributesW(record.target.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES){
            std::array<std::uint8_t,32> digest{};
            if(!sha256FileContent(record.target,digest))return false;
            if(digest==backup->second->sha256){DeleteFileW(record.backup.c_str());return true;} // already old
            // A target holding neither the old nor the new content was modified
            // outside the transaction; never overwrite it blindly.
            if(digest!=record.sha256)return false;
        }
        return restoreFromBackup(record,context.tempDir,backup->second->sha256,backup->second->size);
    }
    }
    return false;
}
// Strictly reverse, fail-closed at the first step that cannot be undone.
bool undoAll(const std::vector<TxJournalRecord>& records,const RollbackContext& context){
    for(auto it=records.rbegin();it!=records.rend();++it)
        if(!undoRecord(*it,context))return false;
    return true;
}
RollbackContext contextFor(const std::vector<TxJournalRecord>& records,TxRegistry* registry,
    const std::wstring& tempDir){
    RollbackContext context;context.registry=registry;context.tempDir=tempDir;
    for(const auto& record:records)
        if(record.op==TxOperation::Backup)context.backups[record.target]=&record;
    return context;
}
bool recheckTarget(TxRegistry* registry,const std::wstring& installRoot,std::wstring& displayVersion){
    if(!registry)return false;
    std::wstring location;std::uint32_t schema=0;
    if(!registry->readString(kRegistrationKey,L"InstallLocation",location)
        || !samePath(location,installRoot)
        || !registry->readDword(kRegistrationKey,L"UpdateIdentitySchema",schema) || schema!=2
        || !registry->readString(kRegistrationKey,L"DisplayVersion",displayVersion))return false;
    const auto marker=GetFileAttributesW((installRoot+L"\\"+kMarkerFile).c_str());
    return marker!=INVALID_FILE_ATTRIBUTES && !(marker&FILE_ATTRIBUTE_DIRECTORY)
        && !(marker&FILE_ATTRIBUTE_REPARSE_POINT);
}
}
struct TxEngine::Impl {
    TxEngineRequest request;
    TxEngineOptions options;
    std::unique_ptr<TxRegistry> ownedRegistry;
    TxRegistry* registry=nullptr;
    bool prepared=false,executed=false;
    std::wstring oldDisplayVersion;
    std::vector<PlannedFile> plan;
    TxEngineResult rejected(const wchar_t* detail){return {TxOutcome::Rejected,detail};}
};
TxEngine::TxEngine(TxEngineRequest request,TxEngineOptions options)
    :impl_(std::make_unique<Impl>()){impl_->request=std::move(request);impl_->options=std::move(options);}
TxEngine::~TxEngine()=default;
TxEngine::TxEngine(TxEngine&&) noexcept=default;
TxEngine& TxEngine::operator=(TxEngine&&) noexcept=default;
// Strictly read-only: image location, path policy, registration, marker,
// manifests, diff, collision scan, volume space and staged payload hashes.
// Nothing here may create or modify a file; the protocol Proceed gate depends
// on it.
TxEngineResult TxEngine::prepare(){
    auto& state=*impl_;
    if(state.prepared)return {TxOutcome::Prepared,{}};
    wchar_t module[32768]{};
    const auto image=state.options.selfImage.empty()
        ?(GetModuleFileNameW(nullptr,module,32768)?std::wstring(module):std::wstring())
        :state.options.selfImage;
    const auto& protectedCheck=state.options.protectedImage;
    const bool imageOk=protectedCheck?protectedCheck(image,state.request.journalRoot)
        :productionProtectedImage(image,state.request.journalRoot);
    if(image.empty() || !imageOk)return state.rejected(L"engine image is not under the protected root");
    if(!state.request.txid || !detail::standardInstallationPath(state.request.installRoot)
        || !detail::standardInstallationPath(state.request.stagingDir)
        || !detail::standardInstallationPath(state.request.journalRoot))
        return state.rejected(L"nonstandard path or transaction");
    state.registry=state.options.registry;
    if(!state.registry){
        state.ownedRegistry=productionRegistry();
        state.registry=state.ownedRegistry.get();
    }
    if(!state.registry || !recheckTarget(state.registry,state.request.installRoot,state.oldDisplayVersion))
        return state.rejected(L"registration, schema or marker mismatch");
    std::vector<TxManifestEntry> oldManifest,newManifest;
    if(parseManifestFile(state.request.installRoot+L"\\"+kInstalledManifest,oldManifest)!=TxManifestError::None)
        return state.rejected(L"installed manifest unreadable (legacy installs upgrade manually)");
    if(parseManifestFile(state.request.stagingDir+L"\\"+kStagedManifest,newManifest)!=TxManifestError::None)
        return state.rejected(L"staged manifest unreadable");
    // Manifest-driven diff only: never scan the directory to guess a delete
    // set, never touch files the old manifest does not own.
    std::map<std::wstring,TxManifestEntry> oldByPath;
    for(const auto& entry:oldManifest)oldByPath[entry.relpath]=entry;
    std::set<std::wstring> newPaths;
    for(const auto& entry:newManifest)newPaths.insert(entry.relpath);
    std::uint64_t stagingBytes=0,backupBytes=0;
    for(const auto& entry:newManifest){
        const auto old=oldByPath.find(entry.relpath);
        if(old!=oldByPath.end() && old->second.size==entry.size && old->second.sha256==entry.sha256)continue;
        PlannedFile file;file.relpath=entry.relpath;file.incoming=entry;
        file.target=targetPathFor(state.request.installRoot,entry.relpath);
        file.staged=targetPathFor(state.request.stagingDir,entry.relpath);
        if(old==oldByPath.end()){
            file.op=TxOperation::Create;
            // Payload may never overwrite a file the old installation did not own.
            if(GetFileAttributesW(file.target.c_str())!=INVALID_FILE_ATTRIBUTES)
                return state.rejected(L"payload collides with an unknown existing file");
        }else{
            file.op=TxOperation::Replace;file.expectedOld=old->second;
            backupBytes=saturated(backupBytes,old->second.size);
        }
        if(!detail::standardInstallationPath(file.target))return state.rejected(L"nonstandard target");
        stagingBytes=saturated(stagingBytes,entry.size);
        state.plan.push_back(std::move(file));
    }
    for(const auto& entry:oldManifest){
        if(newPaths.count(entry.relpath))continue;
        PlannedFile file;file.op=TxOperation::Delete;file.relpath=entry.relpath;file.expectedOld=entry;
        file.target=targetPathFor(state.request.installRoot,entry.relpath);
        if(!detail::standardInstallationPath(file.target))return state.rejected(L"nonstandard target");
        backupBytes=saturated(backupBytes,entry.size);
        state.plan.push_back(std::move(file));
    }
    std::uint64_t oldManifestSize=0;std::array<std::uint8_t,32> ignored{};
    if(!sha256FileContent(state.request.installRoot+L"\\"+kInstalledManifest,ignored,&oldManifestSize))
        return state.rejected(L"installed manifest unreadable");
    backupBytes=saturated(backupBytes,oldManifestSize);
    // Space preflight covers every involved volume: the journal volume carries
    // staging + backup, the installation volume carries the incoming payload
    // bytes. Sharing a volume merges the requirements into one check so a
    // split-layout shortfall never slips into partial replacement.
    const auto& injectedCheck=state.options.volumeCheck;
    const auto preflight=[&](const std::wstring& volumeRoot,std::uint64_t staging,std::uint64_t backup){
        return injectedCheck?injectedCheck(volumeRoot,staging,backup)
            :checkVolumeSpace(volumeRoot,staging,backup);
    };
    const auto journalVolume=canonicalPath(state.request.journalRoot).substr(0,3);
    const auto installVolume=canonicalPath(state.request.installRoot).substr(0,3);
    const bool sameVolume=journalVolume.size()==3 && installVolume.size()==3
        && CompareStringOrdinal(journalVolume.c_str(),3,installVolume.c_str(),3,TRUE)==CSTR_EQUAL;
    // The incoming payload occupies the journal volume once (staged copy) and
    // the installation volume once (placed copy); a shared volume needs both.
    const auto payloadBytes=stagingBytes;
    const bool sufficient=sameVolume
        ?preflight(state.request.journalRoot,saturated(payloadBytes,payloadBytes),backupBytes)==TxVolumeCheck::Ok
        :preflight(state.request.journalRoot,stagingBytes,backupBytes)==TxVolumeCheck::Ok
            && preflight(state.request.installRoot,payloadBytes,0)==TxVolumeCheck::Ok;
    if(!sufficient)
        return state.rejected(L"insufficient volume space for staging, backup and installation");
    for(const auto& file:state.plan){
        if(file.op==TxOperation::Delete)continue;
        std::array<std::uint8_t,32> digest{};std::uint64_t size=0;
        if(!sha256FileContent(file.staged,digest,&size) || digest!=file.incoming.sha256 || size!=file.incoming.size)
            return state.rejected(L"staged payload does not match the new manifest");
    }
    state.prepared=true;
    return {TxOutcome::Prepared,{}};
}
// Journal-first modification sequence; every record is durable before its
// operation runs. Any ordinary failure reverses this run's records in reverse
// order; a rollback that itself fails escalates to authorized recovery.
TxEngineResult TxEngine::execute(){
    auto& state=*impl_;
    if(!state.prepared || state.executed)return state.rejected(L"execute requires a successful prepare");
    state.executed=true;
    auto lease=pinForTransaction(state.request.installRoot);
    if(!lease)return state.rejected(L"installation directory cannot be pinned");
    TxJournal journal;
    const auto opened=journal.open(state.request.journalRoot,state.request.txid,state.options.journal);
    if(opened==TxJournalError::Exists)
        return state.rejected(L"transaction already exists; authorized recovery owns it");
    if(opened!=TxJournalError::None)return state.rejected(L"journal unavailable");
    const auto backupDir=journal.directory()+L"\\backup",tempDir=journal.directory()+L"\\tmp";
    if(!CreateDirectoryW(backupDir.c_str(),nullptr) || !CreateDirectoryW(tempDir.c_str(),nullptr))
        return {TxOutcome::RolledBack,L"transaction subdirectories unavailable"};
    std::vector<TxJournalRecord> records;
    std::uint64_t seq=1;
    const auto fail=[&](TxOutcome outcome,const wchar_t* detail){
        const auto context=contextFor(records,state.registry,tempDir);
        if(!undoAll(records,context))return TxEngineResult{TxOutcome::NeedsAuthorizedRecovery,L"rollback incomplete; scene preserved"};
        return TxEngineResult{outcome,detail};
    };
    const auto append=[&](TxJournalRecord& record){
        record.seq=seq++;
        if(journal.append(record)==TxJournalError::None){records.push_back(record);return true;}
        return false;
    };
    for(const auto& file:state.plan){
        if(file.op==TxOperation::Create){
            TxJournalRecord record{};record.op=TxOperation::Create;record.target=file.target;
            record.sha256=file.incoming.sha256;record.size=file.incoming.size;
            if(!append(record))return fail(TxOutcome::RolledBack,L"journal append failed");
            const auto temp=tempDir+L"\\"+hexSeq(record.seq);
            if(!stageToTemp(file.staged,file.incoming,temp))return fail(TxOutcome::RolledBack,L"staged copy failed");
            if(!ensureParentDirectories(state.request.installRoot,file.relpath)
                || !MoveFileExW(temp.c_str(),file.target.c_str(),MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH)){
                DeleteFileW(temp.c_str());return fail(TxOutcome::RolledBack,L"create placement failed");
            }
            continue;
        }
        // Replace/Delete: journal the backup intent first, then copy the old
        // bytes while proving them against the old manifest. A modified
        // managed file is a conflict: stop and roll back the applied part.
        TxJournalRecord backup{};backup.op=TxOperation::Backup;backup.target=file.target;
        backup.backup=backupDir+L"\\"+hexSeq(seq);
        backup.sha256=file.expectedOld.sha256;backup.size=file.expectedOld.size;
        if(!append(backup))return fail(TxOutcome::RolledBack,L"journal append failed");
        {
            auto source=openManagedRead(file.target);
            std::array<std::uint8_t,32> digest{};std::uint64_t size=0;
            if(!source || !copyWithHash(source.get(),backup.backup,digest,size)
                || digest!=file.expectedOld.sha256 || size!=file.expectedOld.size)
                return fail(TxOutcome::Conflict,L"managed file was modified; refusing to overwrite");
        }
        TxJournalRecord apply{};apply.op=file.op;apply.target=file.target;apply.backup=backup.backup;
        // Delete digests name the old content being removed; Replace digests
        // name the new content being placed.
        apply.sha256=file.op==TxOperation::Delete?file.expectedOld.sha256:file.incoming.sha256;
        apply.size=file.op==TxOperation::Delete?file.expectedOld.size:file.incoming.size;
        if(!append(apply))return fail(TxOutcome::RolledBack,L"journal append failed");
        if(file.op==TxOperation::Delete){
            if(!notReparseFile(file.target) || !DeleteFileW(file.target.c_str()))
                return fail(TxOutcome::RolledBack,L"delete failed");
            continue;
        }
        const auto temp=tempDir+L"\\"+hexSeq(apply.seq);
        if(!stageToTemp(file.staged,file.incoming,temp))return fail(TxOutcome::RolledBack,L"staged copy failed");
        if(!MoveFileExW(temp.c_str(),file.target.c_str(),
            MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH)){
            const auto placementError=GetLastError();
            DeleteFileW(temp.c_str());
            return fail(TxOutcome::RolledBack,(L"replace placement failed "+std::to_wstring(placementError)).c_str());
        }
    }
    // Registration belongs to the same transaction: old DisplayVersion value
    // journaled before the write, restored by recovery.
    {
        TxJournalRecord registration{};registration.op=TxOperation::Registry;
        registration.target=kRegistrationKey;registration.oldRegistryValue=state.oldDisplayVersion;
        if(!append(registration))return fail(TxOutcome::RolledBack,L"journal append failed");
        if(!state.registry->writeString(kRegistrationKey,L"DisplayVersion",state.request.displayVersion))
            return fail(TxOutcome::RolledBack,L"registration write failed");
    }
    // The installed manifest is engine-managed and lands last, itself
    // journaled as backup + replace so recovery restores the old file set's
    // manifest together with the files.
    {
        const auto installed=state.request.installRoot+L"\\"+kInstalledManifest;
        const auto staged=state.request.stagingDir+L"\\"+kStagedManifest;
        TxJournalRecord backup{};backup.op=TxOperation::Backup;backup.target=installed;
        backup.backup=backupDir+L"\\"+hexSeq(seq);
        {
            std::array<std::uint8_t,32> digest{};
            if(!sha256FileContent(installed,digest,&backup.size))return fail(TxOutcome::RolledBack,L"installed manifest unreadable");
            backup.sha256=digest;
        }
        if(!append(backup))return fail(TxOutcome::RolledBack,L"journal append failed");
        {
            auto source=openManagedRead(installed);
            std::array<std::uint8_t,32> digest{};std::uint64_t size=0;
            if(!source || !copyWithHash(source.get(),backup.backup,digest,size))
                return fail(TxOutcome::RolledBack,L"manifest backup failed");
        }
        TxJournalRecord replace{};replace.op=TxOperation::Replace;replace.target=installed;
        replace.backup=backup.backup;
        TxManifestEntry stagedManifest{};stagedManifest.relpath=kStagedManifest;
        {
            std::array<std::uint8_t,32> digest{};
            if(!sha256FileContent(staged,digest,&stagedManifest.size))return fail(TxOutcome::RolledBack,L"staged manifest unreadable");
            stagedManifest.sha256=digest;
        }
        replace.sha256=stagedManifest.sha256;replace.size=stagedManifest.size;
        if(!append(replace))return fail(TxOutcome::RolledBack,L"journal append failed");
        const auto temp=tempDir+L"\\"+hexSeq(replace.seq);
        if(!stageToTemp(staged,stagedManifest,temp))return fail(TxOutcome::RolledBack,L"staged manifest copy failed");
        if(!MoveFileExW(temp.c_str(),installed.c_str(),
            MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temp.c_str());return fail(TxOutcome::RolledBack,L"manifest placement failed");
        }
    }
    TxJournalRecord complete{};complete.op=TxOperation::Complete;
    if(!append(complete))return fail(TxOutcome::RolledBack,L"completion record failed");
    return {TxOutcome::Applied,{}};
}
// Authorized recovery: rechecks the target, then replays the journal strictly
// in reverse. A corrupt journal (torn tail, zero-byte boundary, foreign
// transaction) is surfaced as preserve-the-scene authorized recovery — never
// silently nothing-to-recover, never auto-continued.
TxEngineResult TxEngine::recover(const std::wstring& journalRoot,std::uint64_t txid,
    const std::wstring& installRoot,TxEngineOptions options){
    const auto rejected=[](const wchar_t* detail){return TxEngineResult{TxOutcome::Rejected,detail};};
    wchar_t module[32768]{};
    const auto image=options.selfImage.empty()
        ?(GetModuleFileNameW(nullptr,module,32768)?std::wstring(module):std::wstring())
        :options.selfImage;
    const bool imageOk=options.protectedImage?options.protectedImage(image,journalRoot)
        :productionProtectedImage(image,journalRoot);
    if(image.empty() || !imageOk)return rejected(L"engine image is not under the protected root");
    if(!txid || !detail::standardInstallationPath(journalRoot) || !detail::standardInstallationPath(installRoot))
        return rejected(L"nonstandard path or transaction");
    std::unique_ptr<TxRegistry> owned;
    TxRegistry* registry=options.registry;
    if(!registry){owned=productionRegistry();registry=owned.get();}
    std::wstring displayVersion;
    if(!registry || !recheckTarget(registry,installRoot,displayVersion))
        return rejected(L"registration, schema or marker mismatch");
    const auto directory=journalRoot+L"\\"+hexSeq(txid);
    std::vector<TxJournalRecord> records;
    const auto replayed=TxJournal::replay(directory,txid,records);
    if(replayed==TxJournalError::Corrupt)
        return {TxOutcome::NeedsAuthorizedRecovery,L"journal corrupt; scene preserved for authorized recovery"};
    if(replayed==TxJournalError::Unavailable){
        // A missing journal.log means the transaction never started; a present
        // but unreadable one is a recovery failure.
        return GetFileAttributesW((directory+L"\\journal.log").c_str())==INVALID_FILE_ATTRIBUTES
            ?TxEngineResult{TxOutcome::NothingToRecover,L"transaction never started"}
            :TxEngineResult{TxOutcome::RecoveryFailed,L"journal unreadable"};
    }
    if(replayed!=TxJournalError::None)return {TxOutcome::RecoveryFailed,L"journal replay refused"};
    if(records.empty() || records.back().op==TxOperation::Complete)
        return {TxOutcome::NothingToRecover,{}};
    const auto tempDir=directory+L"\\tmp";
    CreateDirectoryW(tempDir.c_str(),nullptr);
    const auto context=contextFor(records,registry,tempDir);
    if(!undoAll(records,context))
        return {TxOutcome::RecoveryFailed,L"journal undo step failed; scene preserved"};
    return {TxOutcome::Recovered,{}};
}
}
