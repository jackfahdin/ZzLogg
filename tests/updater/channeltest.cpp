#include "localchannel_win_p.h"
#include <aclapi.h>
#include <sddl.h>
#include <thread>
#include <iostream>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
int wmain(int argc,wchar_t**) {
    if(argc==2){Sleep(3000);return 0;}
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}};
    ProcessIdentity self;check(self.open(GetCurrentProcessId()),"real self identity opened");
    {
        TransactionId id{};randomBytes(id.data(),16);LocalChannel server;check(server.create(id),"ACL fixture endpoint created");
        Handle client(CreateFileW(endpointName(id).c_str(),READ_CONTROL|GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
        auto result=GetSecurityInfo(client.get(),SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor);
        HANDLE rawToken=nullptr;OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken);Handle token(rawToken);
        DWORD size=0;GetTokenInformation(token.get(),TokenLogonSid,nullptr,0,&size);std::vector<BYTE> bytes(size);
        bool exact=false;if(size && GetTokenInformation(token.get(),TokenLogonSid,bytes.data(),size,&size) && result==ERROR_SUCCESS && acl && acl->AceCount==1){
            void* entry=nullptr;if(GetAce(acl,0,&entry)){
                auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);auto logon=reinterpret_cast<TOKEN_GROUPS*>(bytes.data());
                exact=ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && logon->GroupCount==1 && EqualSid(&ace->SidStart,logon->Groups[0].Sid);
            }
        }
        check(exact,"pipe DACL permits only exact current logon SID");if(descriptor)LocalFree(descriptor);
    }
    for(auto wrong:{ProcessStamp{GetCurrentProcessId()+1,self.stamp().created},ProcessStamp{GetCurrentProcessId(),self.stamp().created+1}}){
        HANDLE duplicate=nullptr;DuplicateHandle(GetCurrentProcess(),self.handle(),GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS);
        ProcessIdentity invalid;check(!invalid.adopt(duplicate,wrong),"wrong PID or creation time rejected against held handle");
    }
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=L"\""+std::wstring(exe)+L"\" wait";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"different real process starts");
    Handle thread(process.hThread),child(process.hProcess);ProcessIdentity other;check(other.open(process.dwProcessId),"different process identity held");
    for(int mode=0;mode<7;++mode){
        TransactionId id{};randomBytes(id.data(),16);LocalChannel server;check(server.create(id),"exclusive local endpoint created");
        LocalChannel duplicate;check(!duplicate.create(id),"first-instance collision rejected");
        bool clientAccepted=false;
        std::thread client([&]{
            if(mode==2 || mode==3){
                Handle raw(CreateFileW(endpointName(id).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
                if(!raw)return;Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.get();
                std::array<BYTE,61> bytes{};DWORD written=0;
                if(!WriteFile(raw.get(),bytes.data(),mode==2?59:61,&written,&operation) && GetLastError()==ERROR_IO_PENDING){
                    if(WaitForSingleObject(event.get(),1000)!=WAIT_OBJECT_0)CancelIoEx(raw.get(),&operation);
                    GetOverlappedResult(raw.get(),&operation,&written,TRUE);
                }
                return;
            }
            LocalChannel channel;clientAccepted=channel.connect(id,mode==1?other:self,after(500));
            if(!clientAccepted)return;
            if(mode==4){Sleep(100);return;}
            SessionToken token{};token[0]=1;
            channel.send({mode==0?MessageKind::AwaitingAppExit:MessageKind::Hello,id,token},after(500));
            channel.receive(after(500));
        });
        bool accepted=server.accept(mode==0?other:self,after(500));
        if(mode==0)check(!accepted,"server refuses actual wrong client PID");
        else if(mode!=1){
            check(accepted,"real peer accepted");auto message=server.receive(after(mode==4?20:500));
            if(mode==2 || mode==3 || mode==4)check(!message,"truncated oversized or timed-out message rejected");
            else {check(message && message->kind==MessageKind::Hello,"exact message transported");if(message)server.send(*message,after(500));}
        }
        client.join();if(mode==1)check(!clientAccepted,"client refuses wrong actual server PID");
    }
    {TransactionId id{};randomBytes(id.data(),16);LocalChannel server;check(server.create(id),"timeout endpoint created");auto begin=GetTickCount64();
      check(!server.accept(self,after(20)) && GetTickCount64()-begin<1000,"connect timeout drains operation within total deadline");}
    WaitForSingleObject(child.get(),4000);check(!other.alive(),"real process exit observed");
    return failures?1:0;
}
