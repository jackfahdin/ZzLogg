// No Qt dependency: this executable runs after the GUI and its DLLs exit.
#include "transaction.h"
#include "sha256_win.h"
#include "installlock_win.h"
#include "installlock_win_p.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
using namespace zzlogg::portable;
using zzlogg::updater::detail::Handle;
namespace {
void require(bool condition,const std::string& message) { if(!condition) throw std::runtime_error(message); }
void writeStatus(const fs::path& stage,const char* name,const std::string& contents) {
    const auto temporary=stage/(std::string(name)+".tmp");
    {std::ofstream output(temporary,std::ios::binary); output<<contents;output.flush();require(bool(output),"Cannot write helper status");}
    fs::rename(temporary,stage/name);
}
std::string line(std::istream& input) {
    std::string result; require(bool(std::getline(input,result)) && result.size()<=512,"Invalid transaction plan"); return result;
}
std::size_t count(std::istream& input) {
    auto value=line(input); require(!value.empty() && value.find_first_not_of("0123456789")==std::string::npos,"Invalid plan count");
    const auto number=std::stoull(value);require(number>0 && number<=20000,"Invalid plan file count");return static_cast<std::size_t>(number);
}
void parsePlan(const fs::path& path,std::vector<std::string>& oldFiles,std::vector<File>& files) {
    require(fs::file_size(path)<=8*1024*1024,"Transaction plan too large");
    std::ifstream input(path,std::ios::binary);
    require(line(input)=="ZZLOGG_PORTABLE_UPDATE_1","Invalid transaction plan version");
    const auto oldCount=count(input);
    for(std::size_t i=0;i<oldCount;++i) oldFiles.push_back(line(input));
    const auto newCount=count(input);
    for(std::size_t i=0;i<newCount;++i) {
        const auto record=line(input); const auto split=record.find(' ',65);
        require(record.size()>66 && record[64]==' ' && split!=std::string::npos,"Invalid plan record");
        const auto sha=record.substr(0,64),size=record.substr(65,split-65),name=record.substr(split+1);
        require(sha.find_first_not_of("0123456789abcdef")==std::string::npos && !size.empty() &&
            size.find_first_not_of("0123456789")==std::string::npos && safePath(name),"Invalid plan file metadata");
        files.push_back({name,std::stoull(size),sha});
    }
    require(input.peek()==std::char_traits<char>::eof(),"Unexpected plan data");
}
void availableTargets(const fs::path& root,const std::vector<std::string>& oldFiles) {
    // Keep all opens together: failure is detected before any original is moved.
    std::vector<Handle> handles;
    for(const auto& name:oldFiles) {
        const auto path=root/fs::u8path(name); if(!fs::exists(path)) continue;
        Handle handle(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(bool(handle),"A program file is in use or not writable; close all ZzLogg instances: " + name);
        handles.push_back(std::move(handle));
    }
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=6) return 2;
    const fs::path root(argv[1]),stage(argv[2]);
    std::ofstream log;
    Handle mutex; bool mutexOwned=false,parentExited=false,safeStage=false;
    try {
        require(root.is_absolute() && stage.is_absolute(),"Absolute update paths required");
        const auto rootText=root.wstring();
        require(zzlogg::updater::detail::standardInstallationPath(rootText) &&
            GetDriveTypeW(rootText.substr(0,3).c_str())==DRIVE_FIXED,"Portable automatic updates require a local fixed drive");
        validateTreePath(root,"ZzLogg.exe"); validateTreePath(stage,"plan.txt");
        validateTreePath(stage,"portable-update.log");
        const auto stageText=stage.wstring(),rootPrefix=rootText+L"\\";
        require(CompareStringOrdinal(stageText.c_str(),-1,rootText.c_str(),-1,TRUE)!=CSTR_EQUAL &&
            (stageText.size()<rootPrefix.size() || CompareStringOrdinal(stageText.c_str(),static_cast<int>(rootPrefix.size()),rootPrefix.c_str(),-1,TRUE)!=CSTR_EQUAL),
            "Update staging must be outside the portable installation");
        safeStage=true;log.open(stage/L"portable-update.log",std::ios::app);
        require(bool(log),"Cannot open portable update log");
        const auto expectedPlan=fs::path(argv[4]).u8string(),expectedOld=fs::path(argv[5]).u8string();
        require(hashFile(stage/L"plan.txt")==expectedPlan,"Transaction plan changed");
        std::vector<std::string> oldFiles; std::vector<File> files;
        parsePlan(stage/L"plan.txt",oldFiles,files);
        const auto pid=std::stoull(argv[3]);require(pid>0 && pid<=MAXDWORD,"Invalid parent process");
        Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid)));
        require(bool(parent),"The application process is no longer available");
        std::array<wchar_t,32768> image{}; DWORD imageSize=static_cast<DWORD>(image.size());
        const auto expectedImage=(root/L"ZzLogg.exe").wstring();
        require(QueryFullProcessImageNameW(parent.get(),0,image.data(),&imageSize) &&
            CompareStringOrdinal(image.data(),static_cast<int>(imageSize),expectedImage.c_str(),-1,TRUE)==CSTR_EQUAL,
            "Parent executable does not belong to this portable installation");
        std::vector<Handle> directories;
        for(std::size_t end=rootText.find(L'\\',3);;end=rootText.find(L'\\',end+1)) {
            const auto path=rootText.substr(0,end);
            auto directory=zzlogg::updater::detail::openLeasedDirectory(path);
            require(bool(directory),"Cannot lease portable installation directory");
            directories.push_back(std::move(directory));
            if(end==std::wstring::npos) break;
        }
        zzlogg::updater::DirectoryIdentity identity{};
        require(zzlogg::updater::detail::directoryIdentity(directories.back().get(),identity),"Invalid installation identity");
        const auto mutexName=zzlogg::updater::InstallLock::mutexName(identity);
        mutex.reset(OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str()));
        require(bool(mutex),"The application did not reserve this installation for updating");
        // Prevent stage content from changing between hashing and replacement.
        std::vector<Handle> staged;
        for(const auto& file:files) {
            const auto path=stage/L"payload"/fs::u8path(file.path);
            validateTreePath(stage/L"payload",file.path);
            Handle handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            require(bool(handle),"Cannot lock staged file: " + file.path);staged.push_back(std::move(handle));
        }
        require(hashFile(root/L".zzlogg-portable-files.json")==expectedOld,"Installed package ownership changed");
        validateTransaction(root,stage/L"payload",oldFiles,files,hashFile);
        log<<"Validated payload; waiting for application exit\n";log.flush();
        writeStatus(stage,"ready","READY\n");
        const auto deadline=GetTickCount64()+120000;
        for(;;) {
            require(!fs::exists(stage/L"cancel"),"Update cancelled before application exit");
            const auto wait=WaitForSingleObject(parent.get(),100);
            if(wait==WAIT_OBJECT_0) {parentExited=true;break;}
            require(wait==WAIT_TIMEOUT && GetTickCount64()<deadline,"Timed out waiting for application exit");
        }
        require(!fs::exists(stage/L"cancel"),"Update cancelled");
        const auto wait=WaitForSingleObject(mutex.get(),5000);
        require(wait==WAIT_OBJECT_0,"Installation reservation was abandoned or could not be acquired");
        mutexOwned=true;
        require(hashFile(root/L".zzlogg-portable-files.json")==expectedOld,"Installed package ownership changed before replacement");
        availableTargets(root,oldFiles);
        log<<"Replacing package-owned files\n";log.flush();
        replaceFiles(root,stage/L"payload",stage/L"backup",oldFiles,files,hashFile);
        log<<"Replacement complete; backup retained at "<<(stage/L"backup").u8string()<<"\n";log.flush();
        ReleaseMutex(mutex.get());mutexOwned=false;mutex.reset(); directories.clear();staged.clear();
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
        std::wstring command=L"\""+expectedImage+L"\"";
        require(CreateProcessW(expectedImage.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&startup,&process),
            "Update installed successfully, but restarting ZzLogg failed; start it manually");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
        writeStatus(stage,"complete","Update installed and application restarted.\n");return 0;
    } catch(const std::exception& error) {
        log<<"FAILED: "<<error.what()<<"\n";log.flush();
        if(safeStage) {try {writeStatus(stage,"failed",error.what());} catch(...) {}}
        if(mutexOwned) ReleaseMutex(mutex.get());
        if(parentExited) {
            const std::string message=std::string(error.what())+"\n\nUpdate log and recovery backup: "+stage.u8string();
            const int length=MultiByteToWideChar(CP_UTF8,0,message.data(),static_cast<int>(message.size()),nullptr,0);
            std::wstring wide(static_cast<std::size_t>(length),L'\0');
            MultiByteToWideChar(CP_UTF8,0,message.data(),static_cast<int>(message.size()),wide.data(),length);
            MessageBoxW(nullptr,wide.c_str(),L"ZzLogg portable update",MB_OK|MB_ICONERROR);
        }
        return 1;
    }
}
