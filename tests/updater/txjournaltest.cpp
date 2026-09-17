#include "txjournal_win.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace zzlogg::updater;
namespace fs=std::filesystem;
namespace {
int failures=0;
void check(bool value,const char* name) { if(!value) { ++failures; std::cerr<<"FAIL: "<<name<<" (win32 "<<GetLastError()<<")\n"; } }
struct Handle {
    HANDLE value=nullptr;
    ~Handle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
constexpr std::uint64_t kTxId=0x1122334455667788ull;
const std::wstring kTxDir=L"1122334455667788";
std::wstring currentUserSid() {
    Handle token; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)) return {};
    DWORD size=0; GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
    if(!size || size>4096) return {};
    std::vector<BYTE> bytes(size);
    if(!GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size)) return {};
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)) return {};
    std::wstring result(sid); LocalFree(sid); return result;
}
TxJournalOptions userOptions(const std::wstring& user,int& flushCount) {
    TxJournalOptions options; options.writers={user}; options.readers={user};
    options.flush=[&flushCount](void* h){++flushCount;return FlushFileBuffers(h)!=FALSE;}; return options;
}
TxJournalRecord fileRecord(std::uint64_t seq,TxOperation op,const fs::path& base) {
    TxJournalRecord record; record.seq=seq; record.op=op;
    record.target=(base/L"install"/L"ZzLogg.exe").wstring();
    if(op!=TxOperation::Create) record.backup=(base/L"tx"/L"backup"/L"ZzLogg.exe").wstring();
    for(unsigned i=0;i<32;++i) record.sha256[i]=static_cast<std::uint8_t>(i+seq);
    record.size=12345+seq; return record;
}
std::size_t recordBytes(const TxJournalRecord& r) {
    return 68+2*(r.target.size()+r.backup.size()+r.oldRegistryValue.size());
}
std::vector<BYTE> readAll(const fs::path& path) {
    Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
    if(!file.value) return {};
    LARGE_INTEGER size{}; if(!GetFileSizeEx(file.value,&size) || size.QuadPart>(64<<20)) return {};
    std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read=0;
    if(!bytes.empty() && (!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())) return {};
    return bytes;
}
void patch(const fs::path& path,std::uint64_t offset,const void* data,DWORD size) {
    Handle file{CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)};
    LARGE_INTEGER at{}; at.QuadPart=static_cast<LONGLONG>(offset);
    DWORD written=0;
    check(file.value && SetFilePointerEx(file.value,at,nullptr,FILE_BEGIN)
        && WriteFile(file.value,data,size,&written,nullptr) && written==size && FlushFileBuffers(file.value),"corruption fixture written");
}
void truncateAt(const fs::path& path,std::uint64_t size) {
    Handle file{CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)};
    LARGE_INTEGER at{}; at.QuadPart=static_cast<LONGLONG>(size);
    check(file.value && SetFilePointerEx(file.value,at,nullptr,FILE_BEGIN) && SetEndOfFile(file.value),"truncate fixture applied");
}
struct Ace { std::wstring sid; DWORD mask; BYTE flags; };
std::vector<Ace> daclOf(const std::wstring& path,bool directory,bool& protectedDacl) {
    protectedDacl=false; std::vector<Ace> result;
    PACL acl=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    const auto info=DACL_SECURITY_INFORMATION;
    if(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,info,nullptr,nullptr,&acl,nullptr,&descriptor)!=ERROR_SUCCESS) return result;
    SECURITY_DESCRIPTOR_CONTROL control=0; DWORD revision=0;
    if(GetSecurityDescriptorControl(descriptor,&control,&revision)) protectedDacl=(control&SE_DACL_PROTECTED)!=0;
    if(acl) for(DWORD i=0;i<acl->AceCount;++i) {
        void* entry=nullptr; if(!GetAce(acl,i,&entry)) continue;
        const auto* header=static_cast<ACE_HEADER*>(entry);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE) { result.push_back({L"<deny>",0,0}); continue; }
        const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        LPWSTR sid=nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)),&sid)) continue;
        result.push_back({sid,ace->Mask,header->AceFlags}); LocalFree(sid);
    }
    LocalFree(descriptor); return result;
}
}
int wmain() {
    static_assert(!std::is_copy_constructible_v<TxJournal> && std::is_nothrow_move_constructible_v<TxJournal>);
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
    const auto root=fs::path(temp)/(L"ZzLogg-txjournal-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(root/L"txroot");
    const auto txroot=(root/L"txroot").wstring();
    const auto user=currentUserSid();
    check(!user.empty(),"current user SID captured");
    // Append-flush-reopen parse round trip with an injected ACL principal.
    std::vector<TxJournalRecord> written;
    {
        int flushes=0;
        TxJournal journal;
        check(journal.open(txroot,kTxId,userOptions(user,flushes))==TxJournalError::None,"exclusive transaction directory created");
        check(journal.directory()==txroot+L"\\"+kTxDir,"transaction directory named by transaction ID");
        check(flushes==1,"header flushed on open");
        written.push_back(fileRecord(1,TxOperation::Backup,root));
        written.push_back(fileRecord(2,TxOperation::Replace,root));
        TxJournalRecord registry{}; registry.seq=3; registry.op=TxOperation::Registry;
        registry.target=L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg";
        registry.oldRegistryValue=L"24.11"; written.push_back(registry);
        for(const auto& record:written) check(journal.append(record)==TxJournalError::None,"record appended");
        check(flushes==4,"every record written and flushed");
        TxJournalRecord wrongSeq=fileRecord(9,TxOperation::Delete,root);
        check(journal.append(wrongSeq)!=TxJournalError::None,"out-of-order append refused");
        TxJournalRecord complete{}; complete.seq=4; complete.op=TxOperation::Complete;
        check(journal.append(complete)==TxJournalError::None,"complete appended");
        written.push_back(complete);
        TxJournalRecord late{}; late.seq=5; late.op=TxOperation::Complete;
        check(journal.append(late)!=TxJournalError::None,"append after complete refused");
    }
    const auto journalPath=root/L"txroot"/kTxDir/L"journal.log";
    const auto before=readAll(journalPath);
    check(!before.empty(),"journal persisted");
    std::vector<TxJournalRecord> replayed;
    check(TxJournal::replay((root/L"txroot"/kTxDir).wstring(),kTxId,replayed)==TxJournalError::None
        && replayed==written,"replay rebuilds executed set");
    replayed.clear();
    check(TxJournal::replay((root/L"txroot"/kTxDir).wstring(),kTxId,replayed)==TxJournalError::None
        && replayed==written,"replay is idempotent");
    check(readAll(journalPath)==before,"replay leaves journal bytes untouched");
    {
        int flushes=0; TxJournal second;
        check(second.open(txroot,kTxId,userOptions(user,flushes))==TxJournalError::Exists,"existing transaction directory refuses reuse");
    }
    // Injected-principal DACL semantics, read back from the real objects.
    {
        bool prot=false; const auto aces=daclOf((root/L"txroot"/kTxDir).wstring(),true,prot);
        check(prot,"transaction directory DACL is protected from inheritance");
        // The kernel maps generic file masks at assignment: FA -> FILE_ALL_ACCESS,
        // GRGX -> read/execute (0x1200a9).
        constexpr DWORD kReadExecute=0x1200a9;
        const auto writer=std::find_if(aces.begin(),aces.end(),[&](const Ace& a){return a.sid==user && a.mask==DWORD(FILE_ALL_ACCESS);});
        const auto reader=std::find_if(aces.begin(),aces.end(),[&](const Ace& a){return a.sid==user && a.mask==kReadExecute;});
        check(aces.size()==2 && writer!=aces.end() && reader!=aces.end(),"directory DACL grants injected writer modify and read");
        check(writer!=aces.end() && (writer->flags&(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE))==(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE),"writer ACE inherits to children");
        bool fileProt=true; const auto fileAces=daclOf(journalPath.wstring(),false,fileProt);
        const auto inherited=std::find_if(fileAces.begin(),fileAces.end(),[&](const Ace& a){
            return a.sid==user && a.mask==DWORD(FILE_ALL_ACCESS) && (a.flags&INHERITED_ACE)!=0;});
        check(!fileProt && inherited!=fileAces.end(),"journal inherits protected directory ACL");
    }
    // Reader-only injection proves the DACL really gates access: the journal
    // file cannot be created without write rights on the directory.
    {
        int flushes=0; TxJournal denied; TxJournalOptions options; options.writers={L"SY"}; options.readers={user};
        check(denied.open(txroot,0xaaaabbbbccccddddull,options)!=TxJournalError::None,"reader-only principal cannot create journal");
    }
    {
        const auto production=TxJournalOptions::production();
        check(production.writers.size()==2 && production.readers.size()==1
            && production.writers[0]==L"BA" && production.writers[1]==L"SY" && production.readers[0]==L"AU",
            "production ACL is Administrators/SYSTEM write, authenticated users read");
    }
    // Corruption matrix: every defect refuses recovery with zero operations.
    const auto corruptCase=[&](std::uint64_t txid,int recordCount,const char* name,auto mutate)->std::wstring {
        int flushes=0; TxJournal journal;
        const auto hex=L"0123456789abcdef"; std::wstring dir;
        for(int i=15;i>=0;--i) dir+=hex[(txid>>(i*4))&15];
        check(journal.open(txroot,txid,userOptions(user,flushes))==TxJournalError::None,name);
        for(int i=1;i<=recordCount;++i) check(journal.append(fileRecord(i,TxOperation::Backup,root))==TxJournalError::None,name);
        journal=TxJournal{};
        mutate(root/L"txroot"/dir/L"journal.log");
        return (root/L"txroot"/dir).wstring();
    };
    {
        const auto dir=corruptCase(0x1001,2,"torn fixture",[&](const fs::path& p){
            const auto one=fileRecord(1,TxOperation::Backup,root);
            truncateAt(p,32+recordBytes(one)+20); });
        std::vector<TxJournalRecord> out{{fileRecord(7,TxOperation::Delete,root)}};
        check(TxJournal::replay(dir,0x1001,out)==TxJournalError::Corrupt && out.empty(),"torn record refuses recovery with zero operations");
    }
    {
        const auto dir=corruptCase(0x1002,1,"unknown op fixture",[&](const fs::path& p){
            const std::uint32_t bad=99; patch(p,32+8,&bad,4); });
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1002,out)==TxJournalError::Corrupt && out.empty(),"unknown operation refuses recovery");
    }
    {
        const auto dir=corruptCase(0x1003,1,"flags fixture",[&](const fs::path& p){
            const std::uint32_t bad=1; patch(p,32+12,&bad,4); });
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1003,out)==TxJournalError::Corrupt && out.empty(),"unknown flags refuse recovery");
    }
    {
        const auto dir=corruptCase(0x1004,2,"sequence fixture",[&](const fs::path& p){
            const auto one=fileRecord(1,TxOperation::Backup,root);
            const std::uint64_t bad=7; patch(p,32+recordBytes(one),&bad,8); });
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1004,out)==TxJournalError::Corrupt && out.empty(),"out-of-order sequence refuses recovery");
    }
    {
        const auto dir=corruptCase(0x1005,1,"txid fixture",[&](const fs::path&){});
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1006,out)==TxJournalError::Corrupt && out.empty(),"transaction mismatch refuses recovery");
    }
    {
        const auto dir=corruptCase(0x1007,0,"header fixture",[&](const fs::path& p){ truncateAt(p,16); });
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1007,out)==TxJournalError::Corrupt && out.empty(),"truncated header refuses recovery");
    }
    {
        const auto dir=corruptCase(0x1008,0,"empty fixture",[&](const fs::path&){});
        std::vector<TxJournalRecord> out;
        check(TxJournal::replay(dir,0x1008,out)==TxJournalError::None && out.empty(),"fresh journal replays to zero operations");
    }
    // Append-side caps are symmetric with the replay caps: the writer must
    // never build a journal the strict parser would refuse as Corrupt. The
    // flush is stubbed so filling to the caps does not pay real flush cost.
    {
        TxJournalOptions options; options.writers={user}; options.readers={user};
        options.flush=[](void*){return true;};
        const std::uint64_t byteCap=64ull<<20,countCap=100000;
        {
            TxJournal journal;
            check(journal.open(txroot,0x3001,options)==TxJournalError::None,"byte-cap journal opens");
            TxJournalRecord bulky{}; bulky.op=TxOperation::Registry;
            bulky.target=L"HKLM\\SOFTWARE\\ZzLogg\\cap";
            bulky.oldRegistryValue=std::wstring(4096,L'v');
            const auto each=recordBytes(bulky);
            const auto expected=(byteCap-32)/each;
            std::uint64_t appended=0;
            for(;appended<expected+2;++appended){ bulky.seq=appended+1; if(journal.append(bulky)!=TxJournalError::None) break; }
            check(appended==expected && 32+appended*each<=byteCap && 32+(appended+1)*each>byteCap,
                "append refuses the record that would cross the replay byte cap");
            TxJournalRecord complete{}; complete.seq=appended+1; complete.op=TxOperation::Complete;
            check(32+appended*each+recordBytes(complete)<=byteCap,"cap fixture leaves room for Complete");
            check(journal.append(complete)==TxJournalError::None,"byte-cap refusal does not latch the journal failed");
            std::vector<TxJournalRecord> out;
            check(TxJournal::replay(txroot+L"\\0000000000003001",0x3001,out)==TxJournalError::None
                && out.size()==appended+1,"byte-capped journal still replays");
        }
        {
            TxJournal journal;
            check(journal.open(txroot,0x3002,options)==TxJournalError::None,"count-cap journal opens");
            auto tiny=fileRecord(1,TxOperation::Create,root);
            std::uint64_t appended=0;
            for(;appended<countCap+8;++appended){ tiny.seq=appended+1; if(journal.append(tiny)!=TxJournalError::None) break; }
            check(appended==countCap,"append refuses records beyond the replay record cap");
            TxJournalRecord complete{}; complete.seq=countCap+1; complete.op=TxOperation::Complete;
            check(journal.append(complete)!=TxJournalError::None,"Complete beyond the record cap is refused");
            std::vector<TxJournalRecord> out;
            check(TxJournal::replay(txroot+L"\\0000000000003002",0x3002,out)==TxJournalError::None
                && out.size()==countCap,"count-capped journal replays exactly at the cap");
        }
    }
    // Root validation: network and reparse roots are refused; junction fixture.
    {
        int flushes=0; TxJournal journal;
        check(journal.open(L"\\\\localhost\\C$\\txroot",0x2001,userOptions(user,flushes))==TxJournalError::InvalidRoot,"network root refused");
        check(journal.open(L"relative",0x2001,userOptions(user,flushes))==TxJournalError::InvalidRoot,"relative root refused");
    }
    fs::create_directories(root/L"junction"); fs::create_directories(root/L"other");
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
        int flushes=0; TxJournal journal;
        check(journal.open((root/L"junction").wstring(),0x2002,userOptions(user,flushes))!=TxJournalError::None,"reparse root refused");
        std::uint64_t available=0;
        check(checkVolumeSpace((root/L"junction").wstring(),0,0,&available)!=TxVolumeCheck::Ok,"reparse root refused by space preflight");
        RemoveDirectoryW((root/L"junction").c_str());
    }
    // Volume space preflight.
    {
        std::uint64_t available=0;
        check(checkVolumeSpace(txroot,0,0,&available)==TxVolumeCheck::Ok && available>0,"free space reported");
        check(checkVolumeSpace(txroot,1024,2048,&available)==TxVolumeCheck::Ok,"modest requirement passes");
        check(checkVolumeSpace(txroot,~0ull,0,nullptr)==TxVolumeCheck::Insufficient,"huge staging refused without overflow");
        check(checkVolumeSpace(txroot,0,~0ull,nullptr)==TxVolumeCheck::Insufficient,"huge backup refused without overflow");
        check(checkVolumeSpace(txroot,~0ull-1,~0ull-1,nullptr)==TxVolumeCheck::Insufficient,"saturating sum refused");
        check(checkVolumeSpace(txroot+L"\\missing\\deeper",0,0,nullptr)==TxVolumeCheck::Ok,"nearest existing ancestor supplies free space");
        check(checkVolumeSpace(L"\\\\localhost\\C$\\x",0,0,nullptr)==TxVolumeCheck::InvalidRoot,"network root refused by preflight");
        check(checkVolumeSpace(L"relative",0,0,nullptr)==TxVolumeCheck::InvalidRoot,"relative root refused by preflight");
    }
    std::error_code error; fs::remove_all(root,error);
    check(!error,"temporary fixtures removed");
    std::cout<<"txjournal failures: "<<failures<<'\n';
    return failures?1:0;
}
