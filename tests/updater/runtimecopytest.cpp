#include "runtimecopy_win_p.h"
#include "updatertesthelpers.h"
#include <filesystem>
#include <iostream>
using namespace zzlogg::updater::detail;
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    const auto temp=testTempDirectory();
    auto root=fs::path(temp)/(L"ZzLogg-copy-test-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directory(root);fs::create_directory(root/L"install");fs::create_directory(root/L"runtime");
    auto source=root/L"install"/L"source.exe"; fs::copy_file(argv[1],source);
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL: "<<name<<" error="<<GetLastError()<<'\n';}};
    {
        Handle parent(CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        auto created=createExclusiveDirectory(parent.get(),L"atomic-child");
        check(bool(created),"atomic directory creation returns held handle");
        if(created){
            BY_HANDLE_FILE_INFORMATION actual{},reopened{};
            Handle same(CreateFileW((root/L"atomic-child").c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            check(GetFileInformationByHandle(created.get(),&actual) && GetFileInformationByHandle(same.get(),&reopened)
                && actual.dwVolumeSerialNumber==reopened.dwVolumeSerialNumber && actual.nFileIndexHigh==reopened.nFileIndexHigh
                && actual.nFileIndexLow==reopened.nFileIndexLow,"created handle matches exact directory identity");
            check(!MoveFileW((root/L"atomic-child").c_str(),(root/L"atomic-moved").c_str()),"atomic creation immediately pins directory");
        }
        check(!createExclusiveDirectory(parent.get(),L"atomic-child"),"atomic existing name rejected");
        check(!createExclusiveDirectory(parent.get(),L"..\\outside"),"atomic creation rejects traversal");
    }
    RemoveDirectoryW((root/L"atomic-child").c_str());
    std::wstring copied,dir;
    {
        RuntimeCopy copy;
        check(copy.create(source.wstring(),(root/L"runtime").wstring()),"create stable transaction copy");
        if(copy.path().empty()) return 1;
        copied=copy.path();dir=copy.directory();
        check(copy.unchanged() && fs::path(copied).parent_path()!=root,"new private directory and stable identity");
        check(MoveFileW(source.c_str(),(root/L"install"/L"replaced.exe").c_str()),"source released for rename");
        Handle write(CreateFileW(copied.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        check(!write,"copy denies writes");
        check(!DeleteFileW(copied.c_str()),"copy denies deletion");
        check(!MoveFileW(dir.c_str(),(dir+L"-moved").c_str()),"runtime directory denies redirection");
        check(!MoveFileW(root.c_str(),(root.wstring()+L"-moved").c_str()),"ancestor denies redirection");
        RuntimeCopy second;check(second.create((root/L"install"/L"replaced.exe").wstring(),(root/L"runtime").wstring()) && second.path()!=copied,"second transaction cannot reuse first");
    }
    check(!fs::exists(copied) && !fs::exists(dir),"owned exact copy and empty directory cleaned");
    fs::create_directory(root/L"junction");
    auto target=L"\\??\\"+(root/L"runtime").wstring();auto print=(root/L"runtime").wstring();
    struct Junction {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;wchar_t names[1024];} junction{};
    junction.tag=IO_REPARSE_TAG_MOUNT_POINT;junction.subLength=static_cast<WORD>(target.size()*sizeof(wchar_t));
    junction.printOffset=junction.subLength+sizeof(wchar_t);junction.printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
    std::copy(target.begin(),target.end(),junction.names);std::copy(print.begin(),print.end(),junction.names+target.size()+1);
    junction.length=static_cast<WORD>(8+junction.printOffset+junction.printLength+sizeof(wchar_t));
    {Handle link(CreateFileW((root/L"junction").c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));DWORD bytes=0;
      check(DeviceIoControl(link.get(),FSCTL_SET_REPARSE_POINT,&junction,junction.length+8,nullptr,0,&bytes,nullptr),"real reparse fixture created");}
    {RuntimeCopy denied;check(!denied.create((root/L"install"/L"replaced.exe").wstring(),(root/L"junction").wstring()),"reparse runtime base rejected");}
    {Handle parent(CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr));
      check(!createExclusiveDirectory(parent.get(),L"junction"),"atomic creation rejects existing reparse entry");}
    RemoveDirectoryW((root/L"junction").c_str());
    std::error_code ec;fs::remove(root/L"install"/L"replaced.exe",ec);fs::remove(root/L"install",ec);fs::remove(root/L"runtime",ec);fs::remove(root,ec);
    return failures?1:0;
}
