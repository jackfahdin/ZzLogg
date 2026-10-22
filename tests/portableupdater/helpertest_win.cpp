#include "transaction.h"
#include "sha256_win.h"
#include "installlock_win.h"
#include "../updater/updatertesthelpers.h"
#include <fstream>
#include <iostream>
#include <vector>
using namespace zzlogg::portable;
using zzlogg::updater::detail::Handle;
namespace {
void check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
std::wstring widen(const std::string& text) {return {text.begin(),text.end()};}
std::string read(const fs::path& path) {std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void put(const fs::path& path,const std::string& text) {fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";
    std::size_t backslashes=0;
    for(const auto character:value) {
        if(character==L'\\') {++backslashes;continue;}
        result.append(backslashes*(character==L'"'?2:1),L'\\');
        backslashes=0;
        if(character==L'"')result+=L'\\';
        result+=character;
    }
    result.append(backslashes*2,L'\\');
    return result+L'"';
}
Handle launch(const fs::path& exe,const std::vector<std::wstring>& args,const fs::path& cwd) {
    std::wstring command=quote(exe.wstring());
    for(const auto& arg:args) command+=L" "+quote(arg);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
    check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&startup,&info),"CreateProcess failed");
    CloseHandle(info.hThread);return Handle(info.hProcess);
}
bool awaitFile(const fs::path& file,DWORD timeout=15000) {
    const auto deadline=GetTickCount64()+timeout;
    do {if(fs::exists(file)) return true;Sleep(50);} while(GetTickCount64()<deadline);
    return false;
}
int parentMode(const fs::path& root,const fs::path& stage,const fs::path& helper,
    const std::wstring& mode,const std::wstring& dataDirectory) {
    zzlogg::updater::InstallLock lock;
    check(lock.acquire(root.wstring())==zzlogg::updater::InstallLockError::None,"Cannot reserve fixture installation");
    auto child=launch(helper,{root.wstring(),stage.wstring(),std::to_wstring(GetCurrentProcessId()),
        widen(hashFile(stage/L"plan.txt")),widen(hashFile(root/L".zzlogg-portable-files.json")),dataDirectory},stage);
    if(mode==L"corrupt") {
        check(awaitFile(stage/L"failed"),"Corrupt stage did not fail promptly");
        check(!fs::exists(stage/L"ready"),"Corrupt stage was declared ready");
    } else {
        check(awaitFile(stage/L"ready"),"No ready handshake: "+read(stage/L"failed"));
        check(read(root/L"obsolete.dll")=="old library","Helper modified installation before parent exit");
        check(!fs::exists(stage/L"backup"),"Backup began before parent exit");
        put(stage/L"parent-observed-ready","yes");
        if(mode==L"cancel") {
            put(stage/L"cancel","cancel");
            check(WaitForSingleObject(child.get(),10000)==WAIT_OBJECT_0,"Cancellation did not stop helper");
            check(!fs::exists(stage/L"backup"),"Cancelled helper changed files");
        }
    }
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==7 && std::wstring(argv[1])==L"--parent")
            return parentMode(argv[2],argv[3],argv[4],argv[5],argv[6]);
        check(argc==3,"Expected helper and restart-fixture paths");
        wchar_t ownBuffer[32768]{};GetModuleFileNameW(nullptr,ownBuffer,32768);
        const fs::path own(ownBuffer),helper(argv[1]),restart(argv[2]);
        const auto temporaryDirectory=zzlogg::updater::detail::testTempDirectory();
        check(!temporaryDirectory.empty(),"Cannot resolve temporary directory");
        const auto base=fs::path(temporaryDirectory)/(L"zzlogg-portable-native-"+std::to_wstring(GetCurrentProcessId()));
        fs::remove_all(base);
        for(const auto& mode:{L"success",L"cancel",L"corrupt"}) {
            const auto root=base/mode/L"app",stage=base/mode/L"stage";
            const auto dataDirectory=(base/mode/L"custom data root").wstring()+L"\\";
            fs::create_directories(root);fs::create_directories(stage/L"payload");
            fs::create_directories(dataDirectory);
            fs::copy_file(own,root/L"ZzLogg.exe");fs::copy_file(helper,stage/L"helper.exe");
            put(root/L"obsolete.dll","old library");put(root/L".zzlogg-portable-files.json","old manifest");
            put(root/L"data/config/ZzLogg.ini","user settings");put(root/L"notes.txt","user notes");
            fs::copy_file(restart,stage/L"payload/ZzLogg.exe");put(stage/L"payload/.zzlogg-portable-files.json","new manifest");
            std::string plan="ZZLOGG_PORTABLE_UPDATE_1\n3\nZzLogg.exe\nobsolete.dll\n.zzlogg-portable-files.json\n2\n";
            for(const auto* name:{"ZzLogg.exe",".zzlogg-portable-files.json"}) {
                const auto path=stage/L"payload"/name;
                plan+=hashFile(path)+" "+std::to_string(fs::file_size(path))+" "+name+"\n";
            }
            put(stage/L"plan.txt",plan);
            if(std::wstring(mode)==L"corrupt") put(stage/L"payload/.zzlogg-portable-files.json","bad manifest");
            auto parent=launch(root/L"ZzLogg.exe",{L"--parent",root.wstring(),stage.wstring(),
                (stage/L"helper.exe").wstring(),mode,dataDirectory},root);
            check(WaitForSingleObject(parent.get(),25000)==WAIT_OBJECT_0,"Parent fixture timed out");
            DWORD code=0;
            check(GetExitCodeProcess(parent.get(),&code),"Cannot read parent fixture exit code");
            check(code==0,"Parent fixture failed (exit "+std::to_string(code)+"): "+read(stage/L"parent-failed")+read(stage/L"failed"));
            if(std::wstring(mode)==L"success") {
                check(awaitFile(stage/L"complete"),"Helper did not complete: "+read(stage/L"failed"));
                check(awaitFile(root/L"restarted"),"New application did not restart");
                check(read(root/L"restarted")==fs::path(dataDirectory).u8string(),
                    "Restart did not receive the original data directory as one argument");
                check(!fs::exists(root/L"obsolete.dll"),"Obsolete library survived update");
                check(read(stage/L"backup/obsolete.dll")=="old library","Backup missing");
            } else check(read(root/L"obsolete.dll")=="old library","Rejected update changed installation");
            check(read(root/L"data/config/ZzLogg.ini")=="user settings","Settings changed");
            check(read(root/L"notes.txt")=="user notes","Unknown file changed");
        }
        std::error_code cleanup;
        for(unsigned attempt=0;attempt<30;++attempt) {
            fs::remove_all(base,cleanup);if(!cleanup) break;Sleep(100);
        }
        check(!cleanup,"Native fixture cleanup failed");std::cout<<"Native portable helper lifecycle tests passed\n";return 0;
    } catch(const std::exception& error) {
        if(argc==7 && std::wstring(argv[1])==L"--parent")
            std::ofstream(fs::path(argv[3])/L"parent-failed")<<error.what();
        std::cerr<<error.what()<<'\n';return 1;
    }
}
