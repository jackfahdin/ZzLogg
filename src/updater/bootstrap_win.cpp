#include "bootstrap_win_p.h"
#include "stablepackage_p.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace zzlogg::updater::detail {
namespace {
// The data directory is restart context, never installation authority: it
// must be an absolute local or UNC path without control characters.
bool dataDirectoryPlausible(const wchar_t* text,std::size_t length) {
    if(!length)return true;
    for(std::size_t i=0;i<length;++i)if(static_cast<uint16_t>(text[i])<0x20)return false;
    const bool drive=length>=3 && ((text[0]>=L'A' && text[0]<=L'Z') || (text[0]>=L'a' && text[0]<=L'z'))
        && text[1]==L':' && (text[2]==L'\\' || text[2]==L'/');
    const bool unc=length>=2 && text[0]==L'\\' && text[1]==L'\\';
    return drive || unc;
}
}
struct ChildBootstrap::Impl {
    update::detail::StablePackage self;
    // Observer handle on the reserved directory mutex, opened with SYNCHRONIZE
    // before Hello. Never waited on and never released: the handle keeps the
    // named object alive so every new entry/lock stays fail-closed until this
    // child ends. Its existence is coordination continuity, not installation
    // authority.
    Handle directoryObserver;
};
ChildBootstrap::ChildBootstrap()=default;ChildBootstrap::~ChildBootstrap()=default;
bool ChildBootstrap::directoryReserved() const { return impl_ && static_cast<bool>(impl_->directoryObserver); }
bool ChildBootstrap::open(int argc,wchar_t** argv){
    if(argc!=2 || impl_)return false;
    uint64_t value=0;const std::wstring text=argv[1];if(text.empty() || text.size()>20)return false;
    for(auto c:text){if(c<L'0' || c>L'9' || value>((std::numeric_limits<uint64_t>::max)()-9)/10)return false;value=value*10+c-L'0';}
    if(!value)return false;Handle mapping(reinterpret_cast<HANDLE>(value));
    const auto view=MapViewOfFile(mapping.get(),FILE_MAP_READ,0,0,sizeof(BootstrapData));if(!view)return false;
    BootstrapData data{};std::memcpy(&data,view,sizeof(data));UnmapViewOfFile(view);
    // Retired mapping versions are never adopted; the data directory must be
    // NUL-terminated inside its fixed capacity and pass the plausibility scan.
    std::size_t directoryLength=0;
    while(directoryLength<DataDirectoryCapacity && data.dataDirectory[directoryLength])++directoryLength;
    if(data.magic!=0x42555a5a || data.version!=3 || (data.flags&~DirectoryReserved) || !data.parentHandle
        || directoryLength==DataDirectoryCapacity || !dataDirectoryPlausible(data.dataDirectory,directoryLength)
        || endpointName(data.transaction).empty()
        || !encodeMessage({MessageKind::Hello,data.transaction,data.token}))return false;
    ProcessIdentity parent,self;
    if(!parent.adopt(reinterpret_cast<HANDLE>(data.parentHandle),data.parent) || !parent.alive()
        || !self.open(GetCurrentProcessId()) || !self.samePrincipal(parent))return false;
    wchar_t executable[32768]{};auto length=GetModuleFileNameW(nullptr,executable,32768);
    auto lease=std::make_unique<Impl>();
    if(!length || length>=32768 || lease->self.open(executable)!=update::PackageVerificationError::None)return false;
    if(data.flags&DirectoryReserved) {
        // The parent still owns and holds the gate while this observer opens
        // the computed existing object; a missing object means the reservation
        // is gone and the handshake must never start.
        if(!data.directory.volumeSerial)return false;
        const auto name=InstallLock::mutexName(data.directory);
        if(name.empty())return false;
        Handle observer(OpenMutexW(SYNCHRONIZE,FALSE,name.c_str()));
        if(!observer)return false;
        lease->directoryObserver=std::move(observer);
    }
    data_=data;parent_=std::move(parent);impl_=std::move(lease);return true;
}
bool launchCopy(const RuntimeCopy& copy,const TransactionId& transaction,const SessionToken& token,const DirectoryIdentity* reservedIdentity,
    const std::wstring& dataDirectory,ProcessIdentity& child){
    if(!copy.unchanged() || dataDirectory.size()>=DataDirectoryCapacity
        || !dataDirectoryPlausible(dataDirectory.data(),dataDirectory.size()))return false;
    ProcessIdentity parent;if(!parent.open(GetCurrentProcessId()))return false;
    HANDLE raw=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),parent.handle(),GetCurrentProcess(),&raw,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,TRUE,0))return false;
    Handle inheritedParent(raw);
    Handle writable(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(BootstrapData),nullptr));if(!writable)return false;
    auto view=MapViewOfFile(writable.get(),FILE_MAP_WRITE,0,0,sizeof(BootstrapData));if(!view)return false;
    BootstrapData data;data.parentHandle=reinterpret_cast<uint64_t>(inheritedParent.get());data.parent=parent.stamp();data.transaction=transaction;data.token=token;
    if(reservedIdentity){data.flags=DirectoryReserved;data.directory=*reservedIdentity;}
    if(!dataDirectory.empty())std::copy(dataDirectory.begin(),dataDirectory.end(),data.dataDirectory);
    std::memcpy(view,&data,sizeof(data));UnmapViewOfFile(view);
    if(!DuplicateHandle(GetCurrentProcess(),writable.get(),GetCurrentProcess(),&raw,FILE_MAP_READ,TRUE,0))return false;
    Handle inheritedMapping(raw);writable.reset();
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size)return false;
    std::vector<BYTE> storage(size);auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&size))return false;
    HANDLE allowlist[]={inheritedParent.get(),inheritedMapping.get()};
    bool prepared=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowlist,sizeof(allowlist),nullptr,nullptr)!=FALSE;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    std::wstring command=L"\""+copy.path()+L"\" "+std::to_wstring(reinterpret_cast<uint64_t>(inheritedMapping.get()));
    PROCESS_INFORMATION process{};
    // Invalid images must return an error, never wait on a loader dialog.
    // Restrict this change to the calling thread and restore the caller's mode.
    DWORD oldMode=0;
    const bool modeSet=SetThreadErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX,&oldMode)!=FALSE;
    const bool started=prepared && modeSet && copy.unchanged() && CreateProcessW(copy.path().c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,copy.directory().c_str(),&startup.StartupInfo,&process);
    if(modeSet)SetThreadErrorMode(oldMode,nullptr);
    DeleteProcThreadAttributeList(attributes);
    if(!started)return false;
    Handle thread(process.hThread),launched(process.hProcess);FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(launched.get(),&created,&exited,&kernel,&user)){
        child.adopt(launched.release(),{});return false;
    }
    const uint64_t stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),launched.get(),GetCurrentProcess(),&limited,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)){
        child.adopt(launched.release(),{});return false;
    }
    // Failure still transfers process ownership to the coordinator's reaper;
    // no post-launch identity failure can turn start() into an unbounded wait.
    return child.adopt(limited,{process.dwProcessId,stamp});
}
}
