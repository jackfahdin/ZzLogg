#include "processidentity_win_p.h"
#include <sddl.h>
#include <bcrypt.h>
namespace zzlogg::updater::detail {
namespace {
uint64_t timestamp(FILETIME t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
bool tokenInfo(HANDLE token,TOKEN_INFORMATION_CLASS kind,std::vector<BYTE>& bytes) {
    DWORD size=0;GetTokenInformation(token,kind,nullptr,0,&size);
    if(!size || size>1024*1024) return false;
    bytes.resize(size);return GetTokenInformation(token,kind,bytes.data(),size,&size)!=FALSE;
}
bool sidCopy(PSID sid,std::vector<BYTE>& result) {
    if(!IsValidSid(sid))return false;result.resize(GetLengthSid(sid));
    return CopySid(static_cast<DWORD>(result.size()),result.data(),sid)!=FALSE;
}
bool principals(HANDLE process,std::vector<BYTE>& user,std::vector<BYTE>& logon,DWORD& session,bool& elevated) {
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))return false;Handle token(raw);
    std::vector<BYTE> bytes;
    if(!tokenInfo(raw,TokenUser,bytes) || !sidCopy(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,user))return false;
    if(!tokenInfo(raw,TokenGroups,bytes))return false;
    auto groups=reinterpret_cast<TOKEN_GROUPS*>(bytes.data());
    for(DWORD i=0;i<groups->GroupCount;++i)if((groups->Groups[i].Attributes&SE_GROUP_LOGON_ID)==SE_GROUP_LOGON_ID)
        if(!sidCopy(groups->Groups[i].Sid,logon))return false;
    DWORD size=0;TOKEN_ELEVATION elevation{};
    if(logon.empty() || !GetTokenInformation(raw,TokenSessionId,&session,sizeof(session),&size)
        || !GetTokenInformation(raw,TokenElevation,&elevation,sizeof(elevation),&size))return false;
    elevated=elevation.TokenIsElevated!=0;return true;
}
}
bool ProcessIdentity::open(DWORD pid) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    FILETIME created{},exited{},kernel{},user{};
    if(!process || !GetProcessTimes(process.get(),&created,&exited,&kernel,&user))return false;
    return adopt(process.release(),{pid,timestamp(created)});
}
bool ProcessIdentity::adopt(HANDLE raw,ProcessStamp expected) {
    handle_.reset(raw);stamp_=expected;user_.clear();logon_.clear();
    if(!matches(expected) || !principals(raw,user_,logon_,session_,elevated_)){
        // Keep ownership even on failed authentication: a just-created process
        // still needs a lifetime owner. Empty principal/stamp cannot authorize IPC.
        stamp_={};user_.clear();logon_.clear();return false;
    }
    return true;
}
bool ProcessIdentity::matches(ProcessStamp expected) const {
    FILETIME created{},exited{},kernel{},user{};
    return handle_ && expected.pid && GetProcessId(handle_.get())==expected.pid
        && GetProcessTimes(handle_.get(),&created,&exited,&kernel,&user) && timestamp(created)==expected.created;
}
bool ProcessIdentity::samePrincipal(const ProcessIdentity& other) const {
    return handle_ && other.handle_ && !user_.empty() && !logon_.empty()
        && user_==other.user_ && logon_==other.logon_ && session_==other.session_;
}
bool ProcessIdentity::alive() const {return handle_ && WaitForSingleObject(handle_.get(),0)==WAIT_TIMEOUT;}
bool randomBytes(void* bytes,ULONG size){return BCryptGenRandom(nullptr,static_cast<PUCHAR>(bytes),size,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;}
std::wstring logonSecurityDescriptor() {
    std::vector<BYTE> user,logon;DWORD session=0;bool elevated=false;
    if(!principals(GetCurrentProcess(),user,logon,session,elevated))return {};
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(logon.data(),&sid))return {};
    std::wstring result=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);return result;
}
SecurityAttributes::SecurityAttributes() {
    const auto descriptor=logonSecurityDescriptor();
    if(!descriptor.empty() && ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(),SDDL_REVISION_1,&descriptor_,nullptr))
        attributes_={sizeof(attributes_),descriptor_,FALSE};
}
SecurityAttributes::~SecurityAttributes(){if(descriptor_)LocalFree(descriptor_);}
}
