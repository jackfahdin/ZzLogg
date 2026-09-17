#include "txjournal_win.h"
#include "installlock_win_p.h"
#include "runtimecopy_win_p.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
namespace zzlogg::updater {
namespace {
// Binary layout, all integers explicit little-endian. File header is 32 bytes:
// magic(8) "ZZTXJRN1" | version(4) | txid(8) | reserved(12, zero). Each record
// is a 68-byte fixed header seq(8) | op(4) | flags(4) | size(8) | sha256(32) |
// targetLen(4) | backupLen(4) | oldValueLen(4) followed by length-prefixed
// UTF-16 bodies without terminators.
constexpr std::size_t kHeaderBytes=32,kRecordFixed=68;
constexpr std::uint64_t kMaxJournalBytes=64ull<<20;
constexpr std::size_t kMaxPathChars=1024,kMaxValueChars=4096,kMaxRecords=100000;
constexpr std::uint64_t kJournalReserveBytes=1ull<<20;
constexpr BYTE kMagic[8]={'Z','Z','T','X','J','R','N','1'};
void put32(std::vector<BYTE>& out,std::uint32_t v){for(int i=0;i<4;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
void put64(std::vector<BYTE>& out,std::uint64_t v){for(int i=0;i<8;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
void putWString(std::vector<BYTE>& out,const std::wstring& s){
    for(const wchar_t c:s){out.push_back(static_cast<BYTE>(c&0xff));out.push_back(static_cast<BYTE>(c>>8));}
}
bool get32(const BYTE*& at,const BYTE* end,std::uint32_t& v){
    if(static_cast<std::size_t>(end-at)<4)return false;v=0;
    for(int i=0;i<4;++i)v|=std::uint32_t(at[i])<<(i*8);at+=4;return true;
}
bool get64(const BYTE*& at,const BYTE* end,std::uint64_t& v){
    if(static_cast<std::size_t>(end-at)<8)return false;v=0;
    for(int i=0;i<8;++i)v|=std::uint64_t(at[i])<<(i*8);at+=8;return true;
}
bool getWString(const BYTE*& at,const BYTE* end,std::uint32_t chars,std::wstring& out){
    if(chars>kMaxValueChars || static_cast<std::uint64_t>(end-at)<2ull*chars)return false;
    out.clear();out.reserve(chars);
    for(std::uint32_t i=0;i<chars;++i){out+=static_cast<wchar_t>(at[0]|(at[1]<<8));at+=2;}
    return true;
}
std::uint64_t saturatedAdd(std::uint64_t a,std::uint64_t b){return b>~0ull-a?~0ull:a+b;}
bool validRegistryTarget(const std::wstring& target){
    if(target.size()<6 || target.size()>kMaxPathChars)return false;
    if(CompareStringOrdinal(target.data(),5,L"HKLM\\",5,TRUE)!=CSTR_EQUAL)return false;
    for(const auto c:target)if(c<32)return false;
    return true;
}
bool validValue(const std::wstring& value){
    if(value.size()>kMaxValueChars)return false;
    for(const auto c:value)if(c<32 && c!=L'\t')return false;
    return true;
}
bool validRecord(const TxJournalRecord& record,std::uint64_t expectedSeq){
    if(record.seq!=expectedSeq || record.flags!=0 || record.target.size()>kMaxPathChars
        || record.backup.size()>kMaxPathChars)return false;
    switch(record.op){
    case TxOperation::Backup:case TxOperation::Replace:case TxOperation::Delete:
        return detail::standardInstallationPath(record.target)
            && (record.backup.empty() || detail::standardInstallationPath(record.backup))
            && record.oldRegistryValue.empty();
    case TxOperation::Create:
        return detail::standardInstallationPath(record.target) && record.backup.empty()
            && record.oldRegistryValue.empty();
    case TxOperation::Registry:
        return validRegistryTarget(record.target) && record.backup.empty() && validValue(record.oldRegistryValue);
    case TxOperation::Complete:
        return record.target.empty() && record.backup.empty() && record.oldRegistryValue.empty();
    }
    return false;
}
std::vector<BYTE> serialize(const TxJournalRecord& record){
    std::vector<BYTE> out;out.reserve(kRecordFixed+2*(record.target.size()+record.backup.size()+record.oldRegistryValue.size()));
    put64(out,record.seq);put32(out,static_cast<std::uint32_t>(record.op));put32(out,record.flags);
    put64(out,record.size);out.insert(out.end(),record.sha256.begin(),record.sha256.end());
    put32(out,static_cast<std::uint32_t>(record.target.size()));
    put32(out,static_cast<std::uint32_t>(record.backup.size()));
    put32(out,static_cast<std::uint32_t>(record.oldRegistryValue.size()));
    putWString(out,record.target);putWString(out,record.backup);putWString(out,record.oldRegistryValue);
    return out;
}
bool validPrincipal(const std::wstring& sid){
    if(sid.empty() || sid.size()>256)return false;
    for(const auto c:sid)if(!((c>=L'0' && c<=L'9') || (c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z') || c==L'-'))return false;
    return true;
}
PSECURITY_DESCRIPTOR transactionSecurity(const TxJournalOptions& options){
    if(options.writers.empty())return nullptr;
    std::wstring sddl=L"D:P";
    for(const auto& writer:options.writers){if(!validPrincipal(writer))return nullptr;sddl+=L"(A;OICI;FA;;;"+writer+L")";}
    for(const auto& reader:options.readers){if(!validPrincipal(reader))return nullptr;sddl+=L"(A;OICI;GRGX;;;"+reader+L")";}
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return nullptr;
    return descriptor;
}
// Same ancestor-pinning discipline as the install lock: every ancestor is held
// with delete-sharing denied and its identity/path revalidated, so nothing can
// be swapped while the transaction directory is created or read.
bool pinAncestors(const std::wstring& path,std::vector<detail::Handle>& leases){
    for(std::size_t end=2;;){
        const auto current=path.substr(0,end==2?3:end);
        auto handle=detail::openLeasedDirectory(current);
        DirectoryIdentity identity{};
        if(!handle || !detail::directoryIdentity(handle.get(),identity)
            || !detail::directoryMatchesPath(handle.get(),current))return false;
        leases.push_back(std::move(handle));
        if(end==path.size())break;
        end=path.find(L'\\',end+1);if(end==std::wstring::npos)end=path.size();
    }
    return true;
}
std::wstring hexTransactionId(std::uint64_t txid){
    const wchar_t hex[]=L"0123456789abcdef";std::wstring leaf;
    for(int i=15;i>=0;--i)leaf+=hex[(txid>>(i*4))&15];
    return leaf;
}
}
bool operator==(const TxJournalRecord& a,const TxJournalRecord& b){
    return a.seq==b.seq && a.op==b.op && a.flags==b.flags && a.target==b.target && a.backup==b.backup
        && a.sha256==b.sha256 && a.size==b.size && a.oldRegistryValue==b.oldRegistryValue;
}
bool operator!=(const TxJournalRecord& a,const TxJournalRecord& b){return !(a==b);}
TxJournalOptions TxJournalOptions::production(){
    TxJournalOptions options;options.writers={L"BA",L"SY"};options.readers={L"AU"};return options;
}
TxVolumeCheck checkVolumeSpace(const std::wstring& root,std::uint64_t stagingBytes,std::uint64_t backupBytes,std::uint64_t* availableBytes){
    if(availableBytes)*availableBytes=0;
    if(!detail::standardInstallationPath(root) || GetDriveTypeW(root.substr(0,3).c_str())!=DRIVE_FIXED)
        return TxVolumeCheck::InvalidRoot;
    const auto required=saturatedAdd(saturatedAdd(kJournalReserveBytes,stagingBytes),backupBytes);
    std::wstring candidate=root;
    for(int depth=0;depth<64;++depth){
        auto handle=detail::openLeasedDirectory(candidate);
        if(handle){
            DirectoryIdentity identity{};
            if(!detail::directoryIdentity(handle.get(),identity) || !detail::directoryMatchesPath(handle.get(),candidate))
                return TxVolumeCheck::InvalidRoot;
            ULARGE_INTEGER freeToCaller{};
            if(!GetDiskFreeSpaceExW(candidate.c_str(),&freeToCaller,nullptr,nullptr))return TxVolumeCheck::Unavailable;
            if(availableBytes)*availableBytes=freeToCaller.QuadPart;
            return freeToCaller.QuadPart>=required?TxVolumeCheck::Ok:TxVolumeCheck::Insufficient;
        }
        const auto separator=candidate.find_last_of(L'\\');
        if(separator==std::wstring::npos || separator<2)return TxVolumeCheck::Unavailable;
        candidate=candidate.substr(0,separator);
        if(candidate.size()==2)candidate+=L'\\';
    }
    return TxVolumeCheck::Unavailable;
}
struct TxJournal::Impl {
    std::vector<detail::Handle> ancestors;
    detail::Handle directoryLease,file;
    std::wstring directory;
    std::function<bool(void*)> flush;
    std::uint64_t nextSeq=1,writtenBytes=kHeaderBytes;
    bool completed=false,failed=false;
};
TxJournal::TxJournal()=default;
TxJournal::~TxJournal()=default;
TxJournal::TxJournal(TxJournal&&) noexcept=default;
TxJournal& TxJournal::operator=(TxJournal&&) noexcept=default;
TxJournalError TxJournal::open(const std::wstring& root,std::uint64_t txid,const TxJournalOptions& options){
    if(impl_)return TxJournalError::Unavailable;
    if(!detail::standardInstallationPath(root) || GetDriveTypeW(root.substr(0,3).c_str())!=DRIVE_FIXED)
        return TxJournalError::InvalidRoot;
    auto impl=std::make_unique<Impl>();
    impl->flush=options.flush;
    if(!pinAncestors(root,impl->ancestors))return TxJournalError::Unavailable;
    PSECURITY_DESCRIPTOR security=transactionSecurity(options);
    if(!security)return TxJournalError::Unavailable;
    impl->directory=root+L"\\"+hexTransactionId(txid);
    impl->directoryLease=detail::createExclusiveDirectory(impl->ancestors.back().get(),
        impl->directory.substr(root.size()+1),security);
    LocalFree(security);
    if(!impl->directoryLease){
        const auto attributes=GetFileAttributesW(impl->directory.c_str());
        return attributes!=INVALID_FILE_ATTRIBUTES?TxJournalError::Exists:TxJournalError::Unavailable;
    }
    DirectoryIdentity identity{};
    bool ok=detail::directoryIdentity(impl->directoryLease.get(),identity);
    if(ok){
        impl->file=detail::Handle(CreateFileW((impl->directory+L"\\journal.log").c_str(),
            FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        ok=static_cast<bool>(impl->file);
    }
    std::vector<BYTE> header;
    if(ok){
        header.insert(header.end(),std::begin(kMagic),std::end(kMagic));
        put32(header,1);put64(header,txid);
        for(int i=0;i<12;++i)header.push_back(0);
        DWORD written=0;
        ok=WriteFile(impl->file.get(),header.data(),static_cast<DWORD>(header.size()),&written,nullptr)
            && written==header.size()
            && (impl->flush?impl->flush(impl->file.get()):FlushFileBuffers(impl->file.get())!=FALSE);
    }
    if(!ok){
        // Nothing was ever committed; best-effort cleanup of our own creation.
        impl->file.reset();impl->directoryLease.reset();impl->ancestors.clear();
        DeleteFileW((impl->directory+L"\\journal.log").c_str());
        RemoveDirectoryW(impl->directory.c_str());
        return TxJournalError::Unavailable;
    }
    impl_=std::move(impl);
    return TxJournalError::None;
}
TxJournalError TxJournal::append(const TxJournalRecord& record){
    if(!impl_ || impl_->failed || impl_->completed)return TxJournalError::Unavailable;
    if(!validRecord(record,impl_->nextSeq))return TxJournalError::Unavailable;
    const auto bytes=serialize(record);
    // Symmetric with the replay caps (same constants, this file): the writer
    // can never build a journal the strict parser would refuse as Corrupt.
    // A cap refusal is not a write failure and does not latch failed.
    if(impl_->nextSeq>kMaxRecords
        || bytes.size()>kMaxJournalBytes-impl_->writtenBytes)return TxJournalError::Unavailable;
    DWORD written=0;
    if(!WriteFile(impl_->file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)
        || written!=bytes.size()
        || !(impl_->flush?impl_->flush(impl_->file.get()):FlushFileBuffers(impl_->file.get())!=FALSE)){
        impl_->failed=true;return TxJournalError::Unavailable;
    }
    impl_->writtenBytes+=bytes.size();
    if(record.op==TxOperation::Complete)impl_->completed=true;
    ++impl_->nextSeq;
    return TxJournalError::None;
}
const std::wstring& TxJournal::directory() const{
    static const std::wstring empty;return impl_?impl_->directory:empty;
}
TxJournalError TxJournal::replay(const std::wstring& directory,std::uint64_t txid,std::vector<TxJournalRecord>& executed){
    executed.clear();
    if(!detail::standardInstallationPath(directory) || GetDriveTypeW(directory.substr(0,3).c_str())!=DRIVE_FIXED)
        return TxJournalError::InvalidRoot;
    std::vector<detail::Handle> ancestors;
    if(!pinAncestors(directory,ancestors))return TxJournalError::Unavailable;
    // Concurrent read-only replay during a live append is by design: the
    // writer holds journal.log open for the whole transaction, and observers
    // (txengine interruption polling) parse the growing journal, treating a
    // torn tail as Corrupt and retrying. FILE_SHARE_WRITE is what makes that
    // possible — tightening it away made every replay fail with sharing
    // violations until the transaction closed (stage 4 triage #6, revert
    // b349b05d). Do not remove the write share.
    detail::Handle file(CreateFileW((directory+L"\\journal.log").c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file)return TxJournalError::Unavailable;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.get(),&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return TxJournalError::Corrupt;
    const auto size=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    if(size<kHeaderBytes || size>kMaxJournalBytes)return TxJournalError::Corrupt;
    std::vector<BYTE> bytes(static_cast<std::size_t>(size));
    DWORD read=0;
    if(!ReadFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
        return TxJournalError::Corrupt;
    const BYTE* at=bytes.data();const BYTE* const end=at+bytes.size();
    if(!std::equal(std::begin(kMagic),std::end(kMagic),at))return TxJournalError::Corrupt;
    at+=sizeof(kMagic);
    std::uint32_t version=0;std::uint64_t storedTxId=0;
    if(!get32(at,end,version) || version!=1 || !get64(at,end,storedTxId) || storedTxId!=txid)return TxJournalError::Corrupt;
    for(int i=0;i<12;++i,++at)if(*at)return TxJournalError::Corrupt;
    std::uint64_t expectedSeq=1;bool complete=false;std::vector<TxJournalRecord> parsed;
    while(at<end){
        if(static_cast<std::size_t>(end-at)<kRecordFixed || parsed.size()>=kMaxRecords){executed.clear();return TxJournalError::Corrupt;}
        TxJournalRecord record{};std::uint32_t op=0,targetLen=0,backupLen=0,valueLen=0;
        if(!get64(at,end,record.seq) || !get32(at,end,op) || !get32(at,end,record.flags)
            || !get64(at,end,record.size))break;
        if(end-at<32){executed.clear();return TxJournalError::Corrupt;}
        std::copy_n(at,32,record.sha256.begin());at+=32;
        if(!get32(at,end,targetLen) || !get32(at,end,backupLen) || !get32(at,end,valueLen)
            || targetLen>kMaxPathChars || backupLen>kMaxPathChars || valueLen>kMaxValueChars
            || !getWString(at,end,targetLen,record.target) || !getWString(at,end,backupLen,record.backup)
            || !getWString(at,end,valueLen,record.oldRegistryValue)){executed.clear();return TxJournalError::Corrupt;}
        record.op=static_cast<TxOperation>(op);
        if(complete || !validRecord(record,expectedSeq)){executed.clear();return TxJournalError::Corrupt;}
        if(record.op==TxOperation::Complete)complete=true;
        parsed.push_back(std::move(record));++expectedSeq;
    }
    if(at!=end){executed.clear();return TxJournalError::Corrupt;}
    executed=std::move(parsed);
    return TxJournalError::None;
}
}
