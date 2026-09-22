// Transaction engine acceptance suite: in-process differential/recovery
// semantics with injected registry + protected-root stand-ins, and real
// subprocess protocol/kill/recovery round-trips driven through the production
// Coordinator against the handoff fixture's engine mode. Nothing here touches
// the real HKLM, real installation directories, or real UAC.
#include "coordinator_p.h"
#include "txengine_win.h"
#include "txjournal_win.h"
#include "updatertesthelpers.h"
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
namespace {
int failures=0;
void check(bool value,const char* name) { if(!value){++failures;std::cerr<<"FAIL: "<<name<<" (win32 "<<GetLastError()<<")\n";} }
// Builds an in-memory security descriptor from SDDL and evaluates the
// protected-root ACL shape predicate on it: no elevated, Administrators-owned
// directory is needed on disk.
bool aclShapeFromSddl(const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,nullptr)){
        check(false,"sddl parsed"); return false;
    }
    PSID owner=nullptr;PACL dacl=nullptr;BOOL defaulted=FALSE,present=FALSE;
    const bool queried=GetSecurityDescriptorOwner(descriptor,&owner,&defaulted)!=FALSE
        && GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted)!=FALSE;
    const bool result=queried && protectedRootAclShape(owner,present?dacl:nullptr);
    LocalFree(descriptor);
    return result;
}
constexpr std::uint64_t kBigBytes=128ull<<20;
std::wstring hexId(std::uint64_t txid) {
    const wchar_t hex[]=L"0123456789abcdef"; std::wstring out;
    for(int i=15;i>=0;--i) out+=hex[(txid>>(i*4))&15];
    return out;
}
fs::path pathFor(const fs::path& base,const std::wstring& relpath) {
    fs::path path=base;
    for(std::size_t begin=0;;) {
        const auto end=relpath.find(L'/',begin);
        path/=relpath.substr(begin,end==std::wstring::npos?end:end-begin);
        if(end==std::wstring::npos) break;
        begin=end+1;
    }
    return path;
}
void writeText(const fs::path& base,const std::wstring& relpath,const std::string& content) {
    const auto path=pathFor(base,relpath);
    std::error_code ec; fs::create_directories(path.parent_path(),ec);
    std::ofstream out(path,std::ios::binary|std::ios::trunc); out<<content;
}
void writeBig(const fs::path& base,const std::wstring& relpath,std::uint64_t bytes,unsigned seed) {
    const auto path=pathFor(base,relpath);
    std::error_code ec; fs::create_directories(path.parent_path(),ec);
    Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    check(static_cast<bool>(file),"big fixture file created");
    std::vector<BYTE> chunk(4u<<20);
    for(std::size_t i=0;i<chunk.size();++i) chunk[i]=static_cast<BYTE>(seed+i*31u);
    std::uint64_t left=bytes;
    while(left) {
        const auto part=static_cast<DWORD>(std::min<std::uint64_t>(left,chunk.size()));
        DWORD written=0;
        if(!WriteFile(file.get(),chunk.data(),part,&written,nullptr) || written!=part) break;
        left-=part;
    }
    FlushFileBuffers(file.get());
}
std::vector<BYTE> readBytes(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
    if(!file) return {};
    LARGE_INTEGER size{}; if(!GetFileSizeEx(file.get(),&size) || size.QuadPart>(256ll<<20)) return {};
    std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart)); DWORD read=0;
    if(!bytes.empty() && (!ReadFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())) return {};
    return bytes;
}
std::string readText(const fs::path& base,const std::wstring& relpath) {
    const auto bytes=readBytes(pathFor(base,relpath));
    return std::string(bytes.begin(),bytes.end());
}
TxManifestEntry entryFor(const fs::path& base,const std::wstring& relpath) {
    TxManifestEntry entry; entry.relpath=relpath;
    check(sha256FileContent(pathFor(base,relpath).wstring(),entry.sha256,&entry.size),"manifest entry hashed");
    return entry;
}
bool entriesEqual(const std::vector<TxManifestEntry>& a,const std::vector<TxManifestEntry>& b) {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].relpath!=b[i].relpath || a[i].size!=b[i].size || a[i].sha256!=b[i].sha256) return false;
    return true;
}
struct FakeRegistry:TxRegistry {
    std::map<std::wstring,std::wstring> strings;
    std::map<std::wstring,std::uint32_t> dwords;
    std::vector<std::wstring> accessed;
    bool failWrites=false;
    bool readString(const std::wstring& key,const std::wstring& name,std::wstring& value) override {
        accessed.push_back(key); const auto it=strings.find(name);
        if(it==strings.end()) return false; value=it->second; return true;
    }
    bool readDword(const std::wstring& key,const std::wstring& name,std::uint32_t& value) override {
        accessed.push_back(key); const auto it=dwords.find(name);
        if(it==dwords.end()) return false; value=it->second; return true;
    }
    bool writeString(const std::wstring& key,const std::wstring& name,const std::wstring& value) override {
        accessed.push_back(key); if(failWrites) return false; strings[name]=value; return true;
    }
};
TxJournalOptions userJournalOptions(const std::wstring& user) {
    TxJournalOptions options; options.writers={user}; options.readers={user}; return options;
}
TxEngineOptions testOptions(FakeRegistry& registry,const std::wstring& user,bool imageOk=true) {
    TxEngineOptions options; options.journal=userJournalOptions(user); options.registry=&registry;
    options.protectedImage=[imageOk](const std::wstring&,const std::wstring&){return imageOk;};
    return options;
}
using Snapshot=std::map<std::wstring,std::pair<std::uint64_t,std::array<std::uint8_t,32>>>;
Snapshot snapshotDir(const fs::path& dir) {
    Snapshot out; std::error_code ec;
    for(const auto& entry:fs::recursive_directory_iterator(dir,ec)) {
        if(!entry.is_regular_file()) continue;
        std::array<std::uint8_t,32> digest{}; std::uint64_t size=0;
        check(sha256FileContent(entry.path().wstring(),digest,&size),"snapshot hashed");
        out[entry.path().lexically_relative(dir).generic_wstring()]={size,digest};
    }
    return out;
}
struct Scenario {
    fs::path base,install,staging,txroot;
    std::wstring installW,stagingW,txrootW;
    std::uint64_t txid=0;
    std::vector<TxManifestEntry> oldEntries,newEntries;
    FakeRegistry registry;
};
// Baseline differential: keep unchanged, small+nested replaced, created added,
// deleted removed; unknown.txt is user data outside both manifests. With
// withBig, a 128 MiB replaced file widens the kill window after its Backup
// record (order: keep, small, big, nested, created).
Scenario buildScenario(const fs::path& base,std::uint64_t txid,bool withBig=false) {
    Scenario sc{}; sc.base=base; sc.install=base/L"install"; sc.staging=base/L"staging"; sc.txroot=base/L"txroot";
    sc.installW=sc.install.wstring(); sc.stagingW=sc.staging.wstring(); sc.txrootW=sc.txroot.wstring(); sc.txid=txid;
    fs::create_directories(sc.install); fs::create_directories(sc.staging); fs::create_directories(sc.txroot);
    writeText(sc.install,L"keep.txt","keep-content");
    writeText(sc.install,L"small.txt","old-small");
    writeText(sc.install,L"deleted.txt","old-deleted");
    writeText(sc.install,L"sub/nested.txt","old-nested");
    writeText(sc.install,L"unknown.txt","user-data");
    writeText(sc.install,L".zzlogg-install-root","ZzLogg");
    sc.oldEntries={entryFor(sc.install,L"keep.txt"),entryFor(sc.install,L"small.txt"),
        entryFor(sc.install,L"deleted.txt"),entryFor(sc.install,L"sub/nested.txt")};
    if(withBig) { writeBig(sc.install,L"big.bin",kBigBytes,0x11); sc.oldEntries.push_back(entryFor(sc.install,L"big.bin")); }
    check(writeManifestFile(sc.install/L".zzlogg-files.manifest",sc.oldEntries),"old manifest written");
    writeText(sc.staging,L"keep.txt","keep-content");
    writeText(sc.staging,L"small.txt","new-small");
    if(withBig) writeBig(sc.staging,L"big.bin",kBigBytes,0x77);
    writeText(sc.staging,L"sub/nested.txt","new-nested");
    writeText(sc.staging,L"created.txt","new-created");
    sc.newEntries={entryFor(sc.staging,L"keep.txt"),entryFor(sc.staging,L"small.txt")};
    if(withBig) sc.newEntries.push_back(entryFor(sc.staging,L"big.bin"));
    sc.newEntries.push_back(entryFor(sc.staging,L"sub/nested.txt"));
    sc.newEntries.push_back(entryFor(sc.staging,L"created.txt"));
    check(writeManifestFile(sc.staging/L"files.manifest",sc.newEntries),"new manifest written");
    sc.registry.strings={{L"InstallLocation",sc.installW},{L"DisplayVersion",L"1.0.0"}};
    sc.registry.dwords={{L"UpdateIdentitySchema",2}};
    return sc;
}
TxEngineRequest requestFor(const Scenario& sc) {
    return {sc.installW,sc.stagingW,sc.txrootW,sc.txid,L"2.0.0"};
}
std::wstring journalDirOf(const Scenario& sc) { return sc.txrootW+L"\\"+hexId(sc.txid); }
std::vector<TxJournalRecord> replayed(const std::wstring& dir,std::uint64_t txid,TxJournalError* error=nullptr) {
    std::vector<TxJournalRecord> records;
    const auto result=TxJournal::replay(dir,txid,records);
    if(error) *error=result;
    return records;
}
std::vector<TxOperation> opsOf(const std::vector<TxJournalRecord>& records) {
    std::vector<TxOperation> ops; for(const auto& record:records) ops.push_back(record.op); return ops;
}
void setEnv(const wchar_t* name,const std::wstring& value) { SetEnvironmentVariableW(name,value.c_str()); }
void clearEngineEnv() {
    for(const auto* name:{L"ZZLOGG_HANDOFF_FIXTURE",L"ZZLOGG_HANDOFF_RECORD",L"ZZLOGG_TX_INSTALL",
        L"ZZLOGG_TX_STAGING",L"ZZLOGG_TX_TXROOT",L"ZZLOGG_TX_TXID",L"ZZLOGG_TX_VERSION",L"ZZLOGG_TX_FAKEREG"})
        SetEnvironmentVariableW(name,nullptr);
}
void writeFakeRegistryFile(const fs::path& path,const std::wstring& location) {
    std::wofstream out(path,std::ios::trunc);
    out<<L"InstallLocation\tS\t"<<location<<L"\nUpdateIdentitySchema\tD\t2\nDisplayVersion\tS\t1.0.0\n";
}
std::wstring fakeRegistryValue(const fs::path& path,const std::wstring& name) {
    std::wifstream in(path); std::wstring line;
    while(std::getline(in,line)) {
        const auto tab=line.find(L'\t');
        if(tab!=std::wstring::npos && line.substr(0,tab)==name) {
            const auto second=line.find(L'\t',tab+1);
            return second==std::wstring::npos?std::wstring():line.substr(second+1);
        }
    }
    return {};
}
DWORD spawnWait(const fs::path& exe,const std::wstring& arguments,DWORD timeoutMs=60000) {
    std::wstring command=L"\""+exe.wstring()+L"\" "+arguments;
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return 999;
    Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),timeoutMs)!=WAIT_OBJECT_0) { TerminateProcess(child.get(),998); return 998; }
    DWORD code=999; GetExitCodeProcess(child.get(),&code); return code;
}
struct AppStandin { Handle process,thread; ProcessIdentity identity; };
AppStandin launchAppStandin(const fs::path& fixtureExe,DWORD milliseconds) {
    AppStandin standin;
    std::wstring command=L"\""+fixtureExe.wstring()+L"\" --app "+std::to_wstring(milliseconds);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(!CreateProcessW(fixtureExe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return standin;
    standin.process.reset(process.hProcess); standin.thread.reset(process.hThread);
    standin.identity.open(process.dwProcessId);
    return standin;
}
CoordinationResult proceedWhenExited(Coordinator& coordinator,const Handle& appProcess) {
    for(int tries=0;tries<150;++tries) {
        const auto result=coordinator.proceedIfExited(after(200));
        if(result!=CoordinationResult::PeerRunning) return result;
        WaitForSingleObject(appProcess.get(),100);
    }
    return CoordinationResult::PeerRunning;
}
bool pollJournal(const std::wstring& dir,std::uint64_t txid,
    const std::function<bool(const std::vector<TxJournalRecord>&)>& predicate,DWORD timeoutMs) {
    const auto deadline=GetTickCount64()+timeoutMs;
    while(GetTickCount64()<deadline) {
        std::vector<TxJournalRecord> records;
        // A torn tail observed mid-append is retried, never treated as final.
        if(TxJournal::replay(dir,txid,records)==TxJournalError::None && predicate(records)) return true;
        Sleep(2);
    }
    return false;
}
bool pollFileValue(const fs::path& path,const std::wstring& name,const std::wstring& value,DWORD timeoutMs) {
    const auto deadline=GetTickCount64()+timeoutMs;
    while(GetTickCount64()<deadline) {
        if(fakeRegistryValue(path,name)==value) return true;
        Sleep(5);
    }
    return false;
}
bool killChild(const Coordinator& coordinator) {
    Handle terminator(OpenProcess(PROCESS_TERMINATE,FALSE,coordinator.process().stamp().pid));
    return terminator && TerminateProcess(terminator.get(),99)
        && WaitForSingleObject(coordinator.process().handle(),10000)==WAIT_OBJECT_0;
}
std::vector<std::wstring> recordLines(const fs::path& path) {
    std::vector<std::wstring> lines; std::wifstream record(path); std::wstring line;
    while(std::getline(record,line)) lines.push_back(line);
    return lines;
}
// Injected installer launcher: models ShellExecuteEx runas by launching the
// fixture installer (NSIS stand-in) as an ordinary child. The engine then
// connects as a grandchild through the credential file, exactly like the
// production restricted entry.
struct LaunchCapture { std::wstring installer,arguments;DWORD launchedPid=0; };
LauncherOutcome fixtureLaunch(LaunchCapture& capture,const std::wstring& installer,const std::wstring& restrictedSwitch) {
    capture.installer=installer; capture.arguments=restrictedSwitch; capture.launchedPid=0;
    LauncherOutcome outcome; outcome.error=LaunchError::Failed;
    std::wstring command=L"\""+installer+L"\" "+restrictedSwitch;
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(!CreateProcessW(installer.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return outcome;
    Handle thread(process.hThread),launched(process.hProcess);
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(launched.get(),&created,&exited,&kernel,&user)) return outcome;
    const uint64_t stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),launched.get(),GetCurrentProcess(),&limited,
        PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)) return outcome;
    ProcessIdentity identity;
    if(!identity.adopt(limited,{process.dwProcessId,stamp})) return outcome;
    capture.launchedPid=process.dwProcessId;
    outcome.process=std::move(identity); outcome.error=LaunchError::None; return outcome;
}
// Raw manifest byte crafting for parser rejection cases (the production
// serializer refuses unsafe entries by construction, so hostile bytes are
// built by hand).
void p32(std::vector<BYTE>& out,std::uint32_t v){for(int i=0;i<4;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
void p64(std::vector<BYTE>& out,std::uint64_t v){for(int i=0;i<8;++i)out.push_back(static_cast<BYTE>(v>>(i*8)));}
std::vector<BYTE> manifestBytes(const std::vector<std::string>& paths) {
    std::vector<BYTE> out{'Z','Z','T','X','M','A','N','1'};
    p32(out,1); p32(out,static_cast<std::uint32_t>(paths.size()));
    for(const auto& path:paths) {
        p32(out,static_cast<std::uint32_t>(path.size())); p64(out,1);
        for(int i=0;i<32;++i) out.push_back(static_cast<BYTE>(i));
        out.insert(out.end(),path.begin(),path.end());
    }
    return out;
}
void writeRaw(const fs::path& path,const std::vector<BYTE>& bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
}
}
namespace {
void testManifest(const fs::path& root) {
    std::cout<<"txengine manifest format"<<std::endl;
    const auto dir=root/L"manifest"; fs::create_directories(dir);
    const std::vector<TxManifestEntry> entries{
        {L"app.exe",3,{1}},{L"sub/dir/file.txt",7,{2}},{L".zzlogg-install-root",6,{3}}};
    check(writeManifestFile(dir/L"ok.manifest",entries),"manifest serialized");
    std::vector<TxManifestEntry> parsed;
    check(parseManifestFile((dir/L"ok.manifest").wstring(),parsed)==TxManifestError::None && entriesEqual(parsed,entries),
        "manifest round trip preserves entries");
    // Serializer refuses unsafe entries instead of emitting them.
    check(!writeManifestFile(dir/L"no.manifest",std::vector<TxManifestEntry>{{L"../evil.exe",1,{}}}),
        "serializer refuses traversal path");
    std::vector<TxManifestEntry> many(4097);
    check(!writeManifestFile(dir/L"many.manifest",many),"serializer refuses entry count beyond bound");
    for(const auto& hostile:{std::string("../x"),std::string("a/../b"),std::string("/abs"),std::string("c:/x"),
            std::string("c:\\x"),std::string("a\\b"),std::string(""),std::string("a//b"),std::string("a/./b"),
            std::string("a\tb"),std::string("trail /b"),std::string("trail./b"),std::string("."),
            std::string("a/b/")}) {
        writeRaw(dir/L"hostile.manifest",manifestBytes({hostile}));
        check(parseManifestFile((dir/L"hostile.manifest").wstring(),parsed)==TxManifestError::UnsafePath,
            "hostile relative path refused");
    }
    writeRaw(dir/L"dup.manifest",manifestBytes({"A.txt","a.TXT"}));
    check(parseManifestFile((dir/L"dup.manifest").wstring(),parsed)!=TxManifestError::None,
        "case-insensitive duplicate entries refused");
    {auto bad=manifestBytes({"a.txt"}); bad[0]='X'; writeRaw(dir/L"magic.manifest",bad);
      check(parseManifestFile((dir/L"magic.manifest").wstring(),parsed)==TxManifestError::BadFormat,"bad magic refused");}
    {auto bad=manifestBytes({"a.txt"}); bad[8]=2; writeRaw(dir/L"version.manifest",bad);
      check(parseManifestFile((dir/L"version.manifest").wstring(),parsed)==TxManifestError::BadFormat,"bad version refused");}
    {std::vector<BYTE> bad{'Z','Z','T','X','M','A','N','1'}; p32(bad,1); p32(bad,0xffffffffu); writeRaw(dir/L"count.manifest",bad);
      check(parseManifestFile((dir/L"count.manifest").wstring(),parsed)==TxManifestError::BadFormat,"absurd count refused");}
    {auto bad=manifestBytes({"a.txt"}); bad.push_back(0); writeRaw(dir/L"trailing.manifest",bad);
      check(parseManifestFile((dir/L"trailing.manifest").wstring(),parsed)==TxManifestError::BadFormat,"trailing bytes refused");}
    {auto bad=manifestBytes({"a.txt"}); bad.resize(bad.size()-5); writeRaw(dir/L"torn.manifest",bad);
      check(parseManifestFile((dir/L"torn.manifest").wstring(),parsed)==TxManifestError::BadFormat,"torn entry refused");}
    {std::ofstream huge(dir/L"huge.manifest",std::ios::binary); huge.seekp(static_cast<std::streamoff>(kMaxManifestBytes)); huge.put(0);}
    check(parseManifestFile((dir/L"huge.manifest").wstring(),parsed)==TxManifestError::TooLarge,"oversize manifest refused");
    check(parseManifestFile((dir/L"missing.manifest").wstring(),parsed)==TxManifestError::Unreadable,"missing manifest unreadable");
    {const std::string longPath(385,'a'); writeRaw(dir/L"long.manifest",manifestBytes({longPath}));
      check(parseManifestFile((dir/L"long.manifest").wstring(),parsed)==TxManifestError::UnsafePath,"overlong path refused");}
}
void testRunHappy(const fs::path& root,const std::wstring& user) {
    std::cout<<"txengine differential run"<<std::endl;
    auto sc=buildScenario(root/L"happy",0x3c001);
    TxEngine engine(requestFor(sc),testOptions(sc.registry,user));
    check(engine.prepare().outcome==TxOutcome::Prepared,"prepare passes on a coherent target");
    const auto result=engine.execute();
    check(result.outcome==TxOutcome::Applied,"transaction applied");
    check(readText(sc.install,L"small.txt")=="new-small" && readText(sc.install,L"sub/nested.txt")=="new-nested"
        && readText(sc.install,L"created.txt")=="new-created" && readText(sc.install,L"keep.txt")=="keep-content",
        "changed and added files applied");
    check(!fs::exists(sc.install/L"deleted.txt"),"removed set deleted");
    check(readText(sc.install,L"unknown.txt")=="user-data","unknown file preserved");
    check(fs::exists(sc.install/L".zzlogg-install-root"),"install marker retained");
    check(readBytes(sc.install/L".zzlogg-files.manifest")==readBytes(sc.staging/L"files.manifest"),
        "installed manifest replaced with the new payload manifest");
    check(sc.registry.strings[L"DisplayVersion"]==L"2.0.0","registration DisplayVersion updated");
    check(!sc.registry.accessed.empty()
        && std::all_of(sc.registry.accessed.begin(),sc.registry.accessed.end(),
            [](const std::wstring& key){return key==kRegistrationKey;}),
        "every registry access targeted the exact uninstall key");
    const auto records=replayed(journalDirOf(sc),sc.txid);
    const std::vector<TxOperation> expected{TxOperation::Backup,TxOperation::Replace,TxOperation::Backup,
        TxOperation::Replace,TxOperation::Create,TxOperation::Backup,TxOperation::Delete,TxOperation::Registry,
        TxOperation::Backup,TxOperation::Replace,TxOperation::Complete};
    check(opsOf(records)==expected,"journal records backup-before-op in plan order");
    check(records.size()==11 && records[7].target==kRegistrationKey && records[7].oldRegistryValue==L"1.0.0",
        "registry old value journaled before the write");
    // A completed transaction is a zero-op recovery.
    const auto appliedSnapshot=snapshotDir(sc.install);
    FakeRegistry again=sc.registry;
    check(TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(again,user)).outcome==TxOutcome::NothingToRecover,
        "completed journal recovers as a zero operation");
    check(snapshotDir(sc.install)==appliedSnapshot,"completed state stable");
}
void testConflictRollback(const fs::path& root,const std::wstring& user) {
    std::cout<<"txengine conflict stop and rollback"<<std::endl;
    auto sc=buildScenario(root/L"conflict",0x3c002);
    writeText(sc.install,L"sub/nested.txt","tampered-by-user");
    TxEngine engine(requestFor(sc),testOptions(sc.registry,user));
    check(engine.prepare().outcome==TxOutcome::Prepared,"prepare does not modify");
    const auto result=engine.execute();
    check(result.outcome==TxOutcome::Conflict,"modified managed file stops the transaction");
    check(readText(sc.install,L"small.txt")=="old-small","already-applied replace rolled back");
    check(!fs::exists(sc.install/L"created.txt"),"pending create never landed");
    check(readText(sc.install,L"sub/nested.txt")=="tampered-by-user","conflicting file left as the user wrote it");
    check(readText(sc.install,L"deleted.txt")=="old-deleted","pending delete never happened");
    check(sc.registry.strings[L"DisplayVersion"]==L"1.0.0","registration untouched after conflict");
    const auto records=replayed(journalDirOf(sc),sc.txid);
    const std::vector<TxOperation> expected{TxOperation::Backup,TxOperation::Replace,TxOperation::Backup};
    check(opsOf(records)==expected,"journal stops at the conflicting backup");
    // The remaining journal is still recoverable and idempotent.
    FakeRegistry again=sc.registry;
    check(TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(again,user)).outcome==TxOutcome::Recovered,
        "post-conflict journal recovers cleanly");
}
void testRegistryFailureRollback(const fs::path& root,const std::wstring& user) {
    std::cout<<"txengine registry failure rollback"<<std::endl;
    auto sc=buildScenario(root/L"regfail",0x3c003);
    sc.registry.failWrites=true;
    TxEngine engine(requestFor(sc),testOptions(sc.registry,user));
    check(engine.prepare().outcome==TxOutcome::Prepared,"prepare passes");
    check(engine.execute().outcome==TxOutcome::RolledBack,"registry write failure rolls the transaction back");
    check(readText(sc.install,L"small.txt")=="old-small" && readText(sc.install,L"sub/nested.txt")=="old-nested"
        && !fs::exists(sc.install/L"created.txt") && readText(sc.install,L"deleted.txt")=="old-deleted",
        "file set fully restored after registry failure");
    check(sc.registry.strings[L"DisplayVersion"]==L"1.0.0","old registration value retained");
    check(fs::exists(sc.install/L".zzlogg-files.manifest"),"old manifest retained");
    const auto ops=opsOf(replayed(journalDirOf(sc),sc.txid));
    check(std::find(ops.begin(),ops.end(),TxOperation::Registry)!=ops.end()
        && std::find(ops.begin(),ops.end(),TxOperation::Complete)==ops.end(),
        "journal-first: registry record persisted although its write failed; no complete");
}
void testPrepareRejections(const fs::path& root,const std::wstring& user) {
    std::cout<<"txengine verification rejections"<<std::endl;
    std::uint64_t txid=0x3c100;
    const auto reject=[&](const char* name,auto mutate,bool imageOk=true) {
        auto sc=buildScenario(root/(L"reject-"+std::to_wstring(txid)),txid++);
        mutate(sc);
        TxEngine engine(requestFor(sc),testOptions(sc.registry,user,imageOk));
        check(engine.prepare().outcome==TxOutcome::Rejected,name);
        check(!fs::exists(journalDirOf(sc)),"rejection creates no journal");
    };
    reject("engine image outside the protected root is rejected",[](Scenario&){},false);
    reject("registered InstallLocation mismatch rejected",[](Scenario& sc){sc.registry.strings[L"InstallLocation"]=sc.installW+L"\\elsewhere";});
    reject("legacy schema 1 rejected",[](Scenario& sc){sc.registry.dwords[L"UpdateIdentitySchema"]=1;});
    reject("missing DisplayVersion rejected",[](Scenario& sc){sc.registry.strings.erase(L"DisplayVersion");});
    reject("missing install marker rejected",[](Scenario& sc){fs::remove(sc.install/L".zzlogg-install-root");});
    reject("missing old manifest rejected (legacy installs are manual)",[](Scenario& sc){fs::remove(sc.install/L".zzlogg-files.manifest");});
    reject("tampered staging rejected",[](Scenario& sc){writeText(sc.staging,L"small.txt","tampered");});
    reject("absurd declared size refused by space preflight",[](Scenario& sc){
        for(auto& entry:sc.newEntries) if(entry.relpath==L"created.txt") entry.size=~0ull-1;
        writeManifestFile(sc.staging/L"files.manifest",sc.newEntries);});
    reject("payload colliding with an unknown user file rejected",[](Scenario& sc){writeText(sc.install,L"created.txt","user file");});
    {
        auto sc=buildScenario(root/L"reject-order",0x3c1f0);
        TxEngine engine(requestFor(sc),testOptions(sc.registry,user));
        check(engine.execute().outcome==TxOutcome::Rejected,"execute before prepare refused");
    }
    {
        // Install-volume preflight (review finding 1): the journal volume
        // alone (staging + backup) has room, but the merged requirement that
        // includes the payload bytes landing on the installation volume is
        // insufficient. Both roots share this machine's temp volume, so the
        // engine must merge into one call whose staging term carries the
        // incoming payload bytes twice (staged copy + placed copy).
        auto sc=buildScenario(root/L"reject-installvol",0x3c1f1);
        std::uint64_t stagedOnly=0;
        for(const auto& entry:sc.newEntries) if(entry.relpath!=L"keep.txt") stagedOnly+=entry.size;
        struct Call { std::wstring root; std::uint64_t staging,backup; };
        std::vector<Call> calls;
        auto options=testOptions(sc.registry,user);
        options.volumeCheck=[&](const std::wstring& volumeRoot,std::uint64_t staging,std::uint64_t backup){
            calls.push_back({volumeRoot,staging,backup});
            return staging>stagedOnly?TxVolumeCheck::Insufficient:TxVolumeCheck::Ok;
        };
        const auto before=snapshotDir(sc.install);
        TxEngine engine(requestFor(sc),std::move(options));
        check(engine.prepare().outcome==TxOutcome::Rejected,
            "install-volume shortfall rejects before any modification");
        check(!fs::exists(journalDirOf(sc)) && snapshotDir(sc.install)==before,
            "install-volume rejection leaves zero changes");
        check(calls.size()==1 && calls[0].root==sc.txrootW && calls[0].staging==stagedOnly*2,
            "shared volume merges journal and installation requirements into one preflight");
        auto control=testOptions(sc.registry,user);
        TxEngine engine2(requestFor(sc),std::move(control));
        check(engine2.prepare().outcome==TxOutcome::Prepared,"sufficient space passes the merged preflight");
    }
}
// Builds a genuinely journaled interrupted state: backups + records written by
// the real TxJournal, install directory left in the applied state.
void buildInterrupted(const fs::path& base,std::uint64_t txid,const std::wstring& user,
    bool complete,bool foreignKey,Scenario* out) {
    Scenario sc{}; sc.base=base; sc.install=base/L"install"; sc.txroot=base/L"txroot"; sc.txid=txid;
    sc.installW=sc.install.wstring(); sc.txrootW=sc.txroot.wstring();
    fs::create_directories(sc.install); fs::create_directories(sc.txroot);
    writeText(sc.install,L"keep.txt","keep-content");
    writeText(sc.install,L"small.txt","new-small");
    writeText(sc.install,L"sub/nested.txt","new-nested");
    writeText(sc.install,L"created.txt","new-created");
    writeText(sc.install,L"unknown.txt","user-data");
    writeText(sc.install,L".zzlogg-install-root","ZzLogg");
    TxJournal journal;
    check(journal.open(sc.txrootW,txid,userJournalOptions(user))==TxJournalError::None,"interrupted journal created");
    const auto dir=journal.directory();
    const auto backup=[&](const wchar_t* leaf,const std::string& content){
        writeText(fs::path(dir),L"backup/"+std::wstring(leaf),content);
        return (fs::path(dir)/L"backup"/leaf).wstring(); };
    const auto b1=backup(L"b1","old-small"),b2=backup(L"b2","old-nested"),b3=backup(L"b3","old-deleted");
    TxManifestEntry oldSmall{}; check(sha256FileContent(b1,oldSmall.sha256,&oldSmall.size),"backup hashed");
    TxManifestEntry oldNested{}; check(sha256FileContent(b2,oldNested.sha256,&oldNested.size),"backup hashed");
    TxManifestEntry oldDeleted{}; check(sha256FileContent(b3,oldDeleted.sha256,&oldDeleted.size),"backup hashed");
    TxManifestEntry newSmall=entryFor(sc.install,L"small.txt"),newNested=entryFor(sc.install,L"sub/nested.txt"),
        newCreated=entryFor(sc.install,L"created.txt");
    const auto target=[&](const std::wstring& leaf){return sc.installW+L"\\"+leaf;};
    const TxJournalRecord records[]={
        {1,TxOperation::Backup,0,target(L"small.txt"),b1,oldSmall.sha256,oldSmall.size,{}},
        {2,TxOperation::Replace,0,target(L"small.txt"),b1,newSmall.sha256,newSmall.size,{}},
        {3,TxOperation::Backup,0,target(L"sub\\nested.txt"),b2,oldNested.sha256,oldNested.size,{}},
        {4,TxOperation::Replace,0,target(L"sub\\nested.txt"),b2,newNested.sha256,newNested.size,{}},
        {5,TxOperation::Create,0,target(L"created.txt"),{},newCreated.sha256,newCreated.size,{}},
        {6,TxOperation::Backup,0,target(L"deleted.txt"),b3,oldDeleted.sha256,oldDeleted.size,{}},
        {7,TxOperation::Delete,0,target(L"deleted.txt"),b3,oldDeleted.sha256,oldDeleted.size,{}},
        {8,TxOperation::Registry,0,foreignKey?
            std::wstring(L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OtherProduct")
            :std::wstring(kRegistrationKey),{},{},0,L"1.0.0"},
    };
    for(const auto& record:records) check(journal.append(record)==TxJournalError::None,"interrupted record appended");
    if(complete) check(journal.append({9,TxOperation::Complete,0,{},{},{},0,{}})==TxJournalError::None,"complete appended");
    journal=TxJournal{};
    const auto keepEntry=entryFor(sc.install,L"keep.txt");
    const std::vector<TxManifestEntry> oldManifest={{L"keep.txt",keepEntry.size,keepEntry.sha256},
        {L"small.txt",oldSmall.size,oldSmall.sha256},{L"deleted.txt",oldDeleted.size,oldDeleted.sha256},
        {L"sub/nested.txt",oldNested.size,oldNested.sha256}};
    check(writeManifestFile(sc.install/L".zzlogg-files.manifest",oldManifest),"interrupted old manifest written");
    sc.registry.strings={{L"InstallLocation",sc.installW},{L"DisplayVersion",L"2.0.0"}};
    sc.registry.dwords={{L"UpdateIdentitySchema",2}};
    *out=std::move(sc);
}
void testRecovery(const fs::path& root,const std::wstring& user) {
    std::cout<<"txengine idempotent recovery"<<std::endl;
    {
        Scenario sc; buildInterrupted(root/L"recover",0x3c201,user,false,false,&sc);
        const auto result=TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(sc.registry,user));
        check(result.outcome==TxOutcome::Recovered,"interrupted transaction recovered");
        check(readText(sc.install,L"small.txt")=="old-small" && readText(sc.install,L"sub/nested.txt")=="old-nested"
            && readText(sc.install,L"deleted.txt")=="old-deleted","replaced and deleted files restored");
        check(!fs::exists(sc.install/L"created.txt"),"created file removed by recovery");
        check(readText(sc.install,L"unknown.txt")=="user-data","unknown file preserved through recovery");
        check(sc.registry.strings[L"DisplayVersion"]==L"1.0.0","registration old value restored");
        const auto afterFirst=snapshotDir(sc.install);
        const auto registryAfterFirst=sc.registry.strings;
        FakeRegistry second=sc.registry;
        const auto again=TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(second,user));
        check(again.outcome==TxOutcome::Recovered && snapshotDir(sc.install)==afterFirst
            && second.strings==registryAfterFirst,"repeated recovery is a zero operation");
    }
    {
        // Torn journal tail: preserve the scene and demand authorized recovery.
        Scenario sc; buildInterrupted(root/L"torn",0x3c202,user,false,false,&sc);
        const auto journalPath=fs::path(journalDirOf(sc))/L"journal.log";
        const auto bytes=readBytes(journalPath);
        {Handle file(CreateFileW(journalPath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));
          LARGE_INTEGER at{}; at.QuadPart=static_cast<LONGLONG>(bytes.size()-10);
          check(file && SetFilePointerEx(file.get(),at,nullptr,FILE_BEGIN) && SetEndOfFile(file.get()),"torn tail applied");}
        const auto before=snapshotDir(sc.install);
        const auto result=TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(sc.registry,user));
        check(result.outcome==TxOutcome::NeedsAuthorizedRecovery,"corrupt journal surfaced as authorized-recovery-only");
        check(snapshotDir(sc.install)==before && sc.registry.strings[L"DisplayVersion"]==L"2.0.0",
            "corrupt journal leaves the scene exactly as found");
    }
    {
        // Zero-byte boundary: crash between journal.log CREATE_NEW and the
        // header write. Exists on reopen, Corrupt on replay; the engine must
        // surface a diagnosable authorized-recovery result, not hang, not
        // silently report nothing-to-recover, not auto-continue.
        Scenario sc; sc.base=root/L"zerobyte"; sc.install=sc.base/L"install"; sc.txroot=sc.base/L"txroot";
        sc.txid=0x3c203; sc.installW=sc.install.wstring(); sc.txrootW=sc.txroot.wstring();
        fs::create_directories(sc.install); fs::create_directories(sc.txroot);
        writeText(sc.install,L"small.txt","old-small");
        writeText(sc.install,L".zzlogg-install-root","ZzLogg");
        sc.registry.strings={{L"InstallLocation",sc.installW},{L"DisplayVersion",L"1.0.0"}};
        sc.registry.dwords={{L"UpdateIdentitySchema",2}};
        const auto dir=sc.txroot/hexId(sc.txid); fs::create_directory(dir);
        {std::ofstream empty(dir/L"journal.log",std::ios::binary);}
        TxJournalError replayError=TxJournalError::None;
        check(replayed(dir.wstring(),sc.txid,&replayError).empty() && replayError==TxJournalError::Corrupt,
            "zero-byte journal replays as corrupt");
        const auto before=snapshotDir(sc.install);
        const auto result=TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(sc.registry,user));
        check(result.outcome==TxOutcome::NeedsAuthorizedRecovery,"zero-byte journal demands authorized recovery");
        check(snapshotDir(sc.install)==before,"zero-byte journal preserves the scene");
    }
    {
        // The journal layer accepts any HKLM\ registry target; the engine must
        // refuse everything but the exact uninstall key during recovery.
        Scenario sc; buildInterrupted(root/L"foreign",0x3c204,user,false,true,&sc);
        FakeRegistry spy=sc.registry;
        const auto result=TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(spy,user));
        check(result.outcome==TxOutcome::RecoveryFailed,"foreign registry key refuses recovery");
        check(spy.strings[L"DisplayVersion"]==L"2.0.0","foreign key never written");
        check(readText(sc.install,L"small.txt")=="new-small","fail-closed recovery leaves earlier records untouched");
    }
    {
        Scenario sc; buildInterrupted(root/L"complete",0x3c205,user,true,false,&sc);
        const auto before=snapshotDir(sc.install);
        check(TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(sc.registry,user)).outcome==TxOutcome::NothingToRecover,
            "completed journal is nothing to recover");
        check(snapshotDir(sc.install)==before,"completed journal untouched");
    }
    {
        Scenario sc{}; sc.base=root/L"fresh"; sc.txroot=sc.base/L"txroot"; sc.install=sc.base/L"install";
        sc.txid=0x3c206; sc.txrootW=sc.txroot.wstring(); sc.installW=sc.install.wstring();
        fs::create_directories(sc.txroot); fs::create_directories(sc.install);
        writeText(sc.install,L".zzlogg-install-root","ZzLogg");
        sc.registry.strings={{L"InstallLocation",sc.installW},{L"DisplayVersion",L"1.0.0"}};
        sc.registry.dwords={{L"UpdateIdentitySchema",2}};
        {TxJournal journal; check(journal.open(sc.txrootW,sc.txid,userJournalOptions(user))==TxJournalError::None,"fresh journal");}
        check(TxEngine::recover(sc.txrootW,sc.txid,sc.installW,testOptions(sc.registry,user)).outcome==TxOutcome::NothingToRecover,
            "header-only journal is nothing to recover");
    }
}
void testKeyAndImagePolicy(const fs::path& root) {
    std::cout<<"txengine registry key and image policy"<<std::endl;
    check(allowedRegistryKey(kRegistrationKey),"exact uninstall key allowed");
    check(allowedRegistryKey(L"HKLM\\SOFTWARE\\MICROSOFT\\WINDOWS\\CURRENTVERSION\\UNINSTALL\\ZZLOGG"),
        "registry key matching is case-insensitive like the registry itself");
    for(const auto* bad:{L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
            L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg2",
            L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg\\Sub",
            L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg",L"HKLM\\",L""})
        check(!allowedRegistryKey(bad),"non-uninstall keys refused");
    const auto protectedRoot=(root/L"protected").wstring();
    fs::create_directories(root/L"protected"/L"staging");
    check(!productionProtectedImage(L"",protectedRoot),"empty image refused");
    check(!productionProtectedImage(L"relative\\engine.exe",protectedRoot),"relative image refused");
    check(!productionProtectedImage((root/L"elsewhere"/L"engine.exe").wstring(),protectedRoot),
        "image outside the protected root refused");
    // Unprivileged temp roots are user-owned: the production ACL check must
    // refuse them even when the image path nests correctly.
    check(!productionProtectedImage((root/L"protected"/L"staging"/L"ZzLoggUpdateTx.exe").wstring(),protectedRoot),
        "user-owned protected root refused by ACL check");
    // ACL shape predicate against in-memory descriptors. The installer's
    // hardened root grants Authenticated Users read-only (icacls (R) =
    // FILE_GENERIC_READ = 0x120089); that mask carries SYNCHRONIZE and
    // READ_CONTROL, which are not data-write capabilities and must not trip
    // the write-bit scan.
    check(aclShapeFromSddl(L"O:BAG:SYD:(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;OICI;0x120089;;;S-1-5-11)"),
        "installer-hardened root ACL accepted (authenticated users read-only)");
    // Over-narrowing guards: any real write bit for a non-admin still rejects.
    check(!aclShapeFromSddl(L"O:BAD:(A;;0x2;;;S-1-5-11)(A;;FA;;;BA)"),
        "FILE_WRITE_DATA for authenticated users rejected");
    check(!aclShapeFromSddl(L"O:BAD:(A;;0x10000;;;S-1-5-11)(A;;FA;;;BA)"),
        "DELETE for authenticated users rejected");
    check(!aclShapeFromSddl(L"O:BAD:(A;;0x40000;;;S-1-5-11)(A;;FA;;;BA)"),
        "WRITE_DAC for authenticated users rejected");
    check(!aclShapeFromSddl(L"O:S-1-5-11D:(A;;FA;;;BA)"),
        "non-admin owner rejected");
    // Deny ACEs are skipped, matching the engine scan.
    check(aclShapeFromSddl(L"O:BAD:(D;;0x2;;;S-1-5-11)(A;;0x120089;;;S-1-5-11)(A;;FA;;;BA)"),
        "deny ACEs do not count as write grants");
}
}
namespace {
// Drives the fixture's engine mode through the production Coordinator's
// installer chain: credential file + restricted switch + engine grandchild.
struct EngineDrive {
    Scenario sc;
    fs::path record,fakeRegistry;
    Coordinator coordinator;
    AppStandin app;
    LaunchCapture captured;
};
bool startEngine(EngineDrive& drive,const fs::path& fixtureExe,DWORD appMs=1200) {
    writeFakeRegistryFile(drive.fakeRegistry,drive.sc.installW);
    setEnv(L"ZZLOGG_HANDOFF_FIXTURE",L"engine");
    setEnv(L"ZZLOGG_HANDOFF_RECORD",drive.record.wstring());
    setEnv(L"ZZLOGG_TX_INSTALL",drive.sc.installW);
    setEnv(L"ZZLOGG_TX_STAGING",drive.sc.stagingW);
    setEnv(L"ZZLOGG_TX_TXROOT",drive.sc.txrootW);
    setEnv(L"ZZLOGG_TX_VERSION",L"2.0.0");
    setEnv(L"ZZLOGG_TX_FAKEREG",drive.fakeRegistry.wstring());
    drive.app=launchAppStandin(fixtureExe,appMs);
    if(!drive.app.identity.handle()) return false;
    CoordinatorOptions options; options.requireElevatedPeer=false;
    options.launcher=[&](const std::wstring& installer,const std::wstring& restrictedSwitch) {
        return fixtureLaunch(drive.captured,installer,restrictedSwitch); };
    if(!drive.coordinator.startInstaller({fixtureExe.wstring(),drive.sc.installW,{}},
        nullptr,&drive.app.identity,options,nullptr)) return false;
    // The restricted switch names the journal transaction; adopt it for the
    // journal assertions and the authorized-recovery fixture invocations.
    const auto locator=drive.captured.arguments.size()>15?drive.captured.arguments.substr(15):std::wstring();
    drive.sc.txid=parseHexId(locator);
    if(!drive.sc.txid) return false;
    setEnv(L"ZZLOGG_TX_TXID",locator);
    return drive.coordinator.authenticate(after(3000))
        && drive.coordinator.awaitAppExit(after(30000))==CoordinationResult::WaitingForAppExit
        && drive.coordinator.commitExit(after(500));
}
void makeDrive(EngineDrive& drive,const fs::path& base,bool withBig) {
    // The journal txid is coordinator-generated; the placeholder is replaced
    // from the captured restricted switch when the chain starts.
    drive.sc=buildScenario(base,0,withBig);
    drive.record=base/L"record.txt"; drive.fakeRegistry=base/L"fakereg.txt";
}
DWORD runRecoveryFixture(const fs::path& fixtureExe,const EngineDrive& drive) {
    // engine-recover mode speaks no protocol; the environment carries the
    // request. This models the NSIS restricted entry's authorized recovery.
    return spawnWait(fixtureExe,L"--engine-recover");
}
void testProtocolHappy(const fs::path& root,const fs::path& fixtureExe) {
    std::cout<<"txengine protocol round trip"<<std::endl;
    EngineDrive drive; makeDrive(drive,root/L"proto",false);
    check(startEngine(drive,fixtureExe),"engine fixture reaches committed exit");
    check(!fs::exists(journalDirOf(drive.sc)),"no file modification before the Proceed gate");
    check(proceedWhenExited(drive.coordinator,drive.app.process)==CoordinationResult::ProceedSent,"Proceed gated");
    ProcessIdentity self; self.open(GetCurrentProcessId());
    const auto finished=drive.coordinator.finish(after(30000));
    check(finished==(self.elevated()?CoordinationResult::ManualRestartRequired:CoordinationResult::Complete),
        "engine completes the gated transaction");
    check(readText(drive.sc.install,L"small.txt")=="new-small" && !fs::exists(drive.sc.install/L"deleted.txt")
        && readText(drive.sc.install,L"created.txt")=="new-created" && readText(drive.sc.install,L"unknown.txt")=="user-data",
        "transaction applied through the real protocol");
    check(fakeRegistryValue(drive.fakeRegistry,L"DisplayVersion")==L"2.0.0","registration updated through fixture");
    const auto records=replayed(journalDirOf(drive.sc),drive.sc.txid);
    check(!records.empty() && records.back().op==TxOperation::Complete,"transaction journaled complete");
    const std::vector<std::wstring> expected{L"installer switch=/ZzLoggUpgrade="+hexId(drive.sc.txid),
        L"engine txid="+hexId(drive.sc.txid),L"kind=2",L"engine prepare=0",L"kind=4",L"kind=8",L"engine outcome=1"};
    check(recordLines(drive.record)==expected,"engine observed the restricted launch and gated protocol in order");
    // Recovery after a completed transaction is authorized and a zero op.
    const auto before=snapshotDir(drive.sc.install);
    check(runRecoveryFixture(fixtureExe,drive)==0,"recovery after completion exits successfully");
    const auto lines=recordLines(drive.record);
    check(!lines.empty() && lines.back()==L"engine recover=6","completed journal recovers as nothing-to-recover");
    check(snapshotDir(drive.sc.install)==before,"post-completion recovery changes nothing");
    clearEngineEnv();
}
void testViolationRejected(const fs::path& root,const fs::path& fixtureExe) {
    std::cout<<"txengine pre-Proceed modification rejected"<<std::endl;
    EngineDrive drive; makeDrive(drive,root/L"violation",false);
    writeFakeRegistryFile(drive.fakeRegistry,drive.sc.installW);
    setEnv(L"ZZLOGG_HANDOFF_FIXTURE",L"engine-violation");
    setEnv(L"ZZLOGG_HANDOFF_RECORD",drive.record.wstring());
    setEnv(L"ZZLOGG_TX_INSTALL",drive.sc.installW); setEnv(L"ZZLOGG_TX_STAGING",drive.sc.stagingW);
    setEnv(L"ZZLOGG_TX_TXROOT",drive.sc.txrootW);
    setEnv(L"ZZLOGG_TX_VERSION",L"2.0.0"); setEnv(L"ZZLOGG_TX_FAKEREG",drive.fakeRegistry.wstring());
    CoordinatorOptions options; options.requireElevatedPeer=false;
    options.launcher=[&](const std::wstring& installer,const std::wstring& restrictedSwitch) {
        return fixtureLaunch(drive.captured,installer,restrictedSwitch); };
    check(drive.coordinator.startInstaller({fixtureExe.wstring(),drive.sc.installW,{}},nullptr,nullptr,options,nullptr)
        && drive.coordinator.authenticate(after(3000))
        && drive.coordinator.awaitAppExit(after(30000))==CoordinationResult::WaitingForAppExit
        && drive.coordinator.commitExit(after(500)),"violation chain reaches committed exit");
    drive.sc.txid=parseHexId(drive.captured.arguments.substr(15));
    check(drive.sc.txid!=0,"violation chain locator captured");
    check(drive.coordinator.finish(after(10000))==CoordinationResult::Failed,"Complete before Proceed is rejected");
    check(fs::exists(drive.sc.install/L"violation-before-proceed.txt"),"violation fixture really wrote early");
    check(!fs::exists(journalDirOf(drive.sc)),"rejected violation never opened a journal");
    const auto lines=recordLines(drive.record);
    check(std::find(lines.begin(),lines.end(),L"write")!=lines.end()
        && std::find(lines.begin(),lines.end(),L"kind=8")==lines.end(),"pretend write is never gated through");
    fs::remove(drive.sc.install/L"violation-before-proceed.txt");
    clearEngineEnv();
}
void testKillMidBackup(const fs::path& root,const fs::path& fixtureExe) {
    std::cout<<"txengine interruption during backup"<<std::endl;
    EngineDrive drive; makeDrive(drive,root/L"killa",true);
    check(startEngine(drive,fixtureExe),"engine reaches committed exit with a large payload");
    check(proceedWhenExited(drive.coordinator,drive.app.process)==CoordinationResult::ProceedSent,"Proceed gated");
    const auto bigTarget=drive.sc.installW+L"\\big.bin";
    check(pollJournal(journalDirOf(drive.sc),drive.sc.txid,[&](const std::vector<TxJournalRecord>& records){
        return std::any_of(records.begin(),records.end(),[&](const TxJournalRecord& record){
            return record.op==TxOperation::Backup && record.target==bigTarget;});},60000),
        "kill point: big backup journaled");
    check(killChild(drive.coordinator),"engine process interrupted mid-transaction");
    const auto records=replayed(journalDirOf(drive.sc),drive.sc.txid);
    check(!records.empty() && records.back().op!=TxOperation::Complete,"interrupted journal left incomplete");
    check(readText(drive.sc.install,L"small.txt")=="new-small","small file was applied before the kill");
    // Authorized recovery reverses the journaled remainder, idempotently.
    check(runRecoveryFixture(fixtureExe,drive)==0,"authorized recovery succeeds");
    auto lines=recordLines(drive.record);
    check(!lines.empty() && lines.back()==L"engine recover=5","recovery reported recovered");
    check(readText(drive.sc.install,L"small.txt")=="old-small" && readText(drive.sc.install,L"sub/nested.txt")=="old-nested"
        && readText(drive.sc.install,L"deleted.txt")=="old-deleted" && !fs::exists(drive.sc.install/L"created.txt")
        && readText(drive.sc.install,L"unknown.txt")=="user-data","install directory restored to the old file set");
    check(fakeRegistryValue(drive.fakeRegistry,L"DisplayVersion")==L"1.0.0","registration still at the old version");
    {
        std::array<std::uint8_t,32> digest{}; std::uint64_t size=0;
        check(sha256FileContent((drive.sc.install/L"big.bin").wstring(),digest,&size)
            && digest==drive.sc.oldEntries.back().sha256,"big file never modified");
    }
    const auto afterFirst=snapshotDir(drive.sc.install);
    check(runRecoveryFixture(fixtureExe,drive)==0,"repeated recovery succeeds");
    lines=recordLines(drive.record);
    check(lines.back()==L"engine recover=5" && snapshotDir(drive.sc.install)==afterFirst,
        "repeated recovery is a zero operation");
    clearEngineEnv();
}
void testKillAfterRegistry(const fs::path& root,const fs::path& fixtureExe) {
    std::cout<<"txengine interruption after registration write"<<std::endl;
    EngineDrive drive; makeDrive(drive,root/L"killb",false);
    check(startEngine(drive,fixtureExe),"engine reaches committed exit");
    check(proceedWhenExited(drive.coordinator,drive.app.process)==CoordinationResult::ProceedSent,"Proceed gated");
    // The fixture's fake registry persists the new value, then sleeps; polling
    // the file makes the kill land deterministically inside that window with
    // every file operation already journaled and applied.
    check(pollFileValue(drive.fakeRegistry,L"DisplayVersion",L"2.0.0",60000),
        "kill point: registration write journaled and applied");
    check(killChild(drive.coordinator),"engine process interrupted after the registration write");
    check(runRecoveryFixture(fixtureExe,drive)==0,"authorized recovery succeeds");
    const auto lines=recordLines(drive.record);
    check(!lines.empty() && lines.back()==L"engine recover=5","recovery reported recovered");
    check(fakeRegistryValue(drive.fakeRegistry,L"DisplayVersion")==L"1.0.0","registration old value restored");
    check(readText(drive.sc.install,L"small.txt")=="old-small" && readText(drive.sc.install,L"sub/nested.txt")=="old-nested"
        && readText(drive.sc.install,L"deleted.txt")=="old-deleted" && !fs::exists(drive.sc.install/L"created.txt")
        && readText(drive.sc.install,L"unknown.txt")=="user-data","full file set restored");
    check(readBytes(drive.sc.install/L".zzlogg-files.manifest")!=readBytes(drive.sc.staging/L"files.manifest"),
        "new manifest never landed before the kill");
    const auto afterFirst=snapshotDir(drive.sc.install);
    check(runRecoveryFixture(fixtureExe,drive)==0,"repeated recovery succeeds");
    check(recordLines(drive.record).back()==L"engine recover=5" && snapshotDir(drive.sc.install)==afterFirst,
        "repeated recovery is a zero operation");
    clearEngineEnv();
}
void testProductionExe(const fs::path& root,const fs::path& txExe) {
    std::cout<<"txengine production executable closure"<<std::endl;
    check(spawnWait(txExe,L"")==2,"production engine without arguments is a usage rejection");
    check(spawnWait(txExe,L"12345 --install x --staging y --txroot z --txid 0000000000000001 --version 2")==2,
        "unknown argv tokens are usage rejections");
    check(spawnWait(txExe,L"--install x --staging y --txroot z --txid 0000000000000001 --version 2")==41,
        "missing credential file rejects before any handshake");
    check(spawnWait(txExe,L"--install x --staging y --txroot z --txid 0000000000000001 --version 2 f1e2d3c4b5a69788")==2,
        "a token on the command line is never accepted");
    for(const auto* badTxid:{L"0000000000000000",L"00000000000000AA",L"abc"}) {
        check(spawnWait(txExe,L"--install x --staging y --txroot z --version 2 --txid "+std::wstring(badTxid))==2,
            "txid must be exactly 16 lowercase hex nonzero");
    }
    const auto base=root/L"production"; const auto install=base/L"install"; const auto txroot=base/L"txroot";
    fs::create_directories(install); fs::create_directories(txroot);
    writeText(install,L"keep.txt","keep-content");
    writeText(install,L".zzlogg-install-root","ZzLogg");
    const auto before=snapshotDir(install);
    const auto code=spawnWait(txExe,L"--recover --install \""+install.wstring()+L"\" --txroot \""+txroot.wstring()
        +L"\" --txid 00000000000000aa");
    check(code==42,"unprivileged recovery target refused before any modification");
    check(snapshotDir(install)==before,"refused recovery touched nothing");
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3){std::cerr<<"usage: txenginetest <handofffixture> <ZzLoggUpdateTx>\n";return 2;}
    const auto temp=detail::testTempDirectory();
    const auto root=fs::path(temp)/(L"ZzLogg-txengine-test-"+std::to_wstring(GetCurrentProcessId())+L"-"
        +std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    const auto user=currentUserSid();
    check(!user.empty(),"current user SID captured");
    const auto fixtureExe=fs::path(argv[1]).make_preferred();
    const auto txExe=fs::path(argv[2]).make_preferred();
    testManifest(root);
    testRunHappy(root,user);
    testConflictRollback(root,user);
    testRegistryFailureRollback(root,user);
    testPrepareRejections(root,user);
    testRecovery(root,user);
    testKeyAndImagePolicy(root);
    testProtocolHappy(root,fixtureExe);
    testViolationRejected(root,fixtureExe);
    testKillMidBackup(root,fixtureExe);
    testKillAfterRegistry(root,fixtureExe);
    testProductionExe(root,txExe);
    clearEngineEnv();
    std::error_code ec; fs::remove_all(root,ec);
    check(!ec,"temporary fixtures removed");
    std::cout<<"txengine failures: "<<failures<<'\n';
    return failures?1:0;
}
