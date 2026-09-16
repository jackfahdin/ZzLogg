#include "localchannel_win_p.h"
#include <algorithm>
namespace zzlogg::updater::detail {
Deadline after(DWORD ms){return GetTickCount64()+ms;}
DWORD remaining(Deadline deadline){auto now=GetTickCount64();return now>=deadline?0:static_cast<DWORD>((std::min)(deadline-now,ULONGLONG(MAXDWORD-1)));}
namespace {
bool completeIo(HANDLE pipe,OVERLAPPED& operation,Deadline deadline,DWORD& bytes) {
    if(WaitForSingleObject(operation.hEvent,remaining(deadline))==WAIT_OBJECT_0)
        return GetOverlappedResult(pipe,&operation,&bytes,FALSE)!=FALSE;
    // OVERLAPPED, event and buffers outlive cancellation completion.
    CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&bytes,TRUE);return false;
}
bool peer(HANDLE pipe,const ProcessIdentity& expected,bool server) {
    ULONG pid=0;ProcessIdentity self;
    return expected.alive() && expected.matches(expected.stamp()) && self.open(GetCurrentProcessId()) && expected.samePrincipal(self)
        && (server?GetNamedPipeClientProcessId(pipe,&pid):GetNamedPipeServerProcessId(pipe,&pid)) && expected.stamp().pid==pid;
}
}
std::wstring endpointName(const TransactionId& id){
    if(std::all_of(id.begin(),id.end(),[](auto b){return b==0;}))return {};
    std::wstring name=L"\\\\.\\pipe\\ZzLogg.Update.";constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto b:id){name+=hex[b>>4];name+=hex[b&15];}return name;
}
bool LocalChannel::create(const TransactionId& id){
    close();auto name=endpointName(id);SecurityAttributes security;if(name.empty() || !security.get())return false;
    pipe_.reset(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,60,60,0,security.get()));return bool(pipe_);
}
bool LocalChannel::accept(const ProcessIdentity& expected,Deadline deadline){
    if(!pipe_ || !remaining(deadline)){close();return false;}
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event){close();return false;}
    OVERLAPPED operation{};operation.hEvent=event.get();DWORD bytes=0;
    bool connected=ConnectNamedPipe(pipe_.get(),&operation)!=FALSE;
    if(!connected){const auto error=GetLastError();connected=error==ERROR_PIPE_CONNECTED || (error==ERROR_IO_PENDING && completeIo(pipe_.get(),operation,deadline,bytes));}
    if(!connected || !peer(pipe_.get(),expected,true)){close();return false;}return true;
}
bool LocalChannel::connect(const TransactionId& id,const ProcessIdentity& expected,Deadline deadline){
    close();auto name=endpointName(id);if(name.empty())return false;
    while(remaining(deadline) && expected.alive()) {
        pipe_.reset(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        if(pipe_)break;
        if(GetLastError()!=ERROR_PIPE_BUSY)return false;
        WaitNamedPipeW(name.c_str(),(std::min)(remaining(deadline),DWORD(20)));
    }
    DWORD mode=PIPE_READMODE_MESSAGE;
    if(!pipe_ || !SetNamedPipeHandleState(pipe_.get(),&mode,nullptr,nullptr) || !peer(pipe_.get(),expected,false)){close();return false;}return true;
}
bool LocalChannel::transfer(bool write,void* data,DWORD size,Deadline deadline){
    if(!pipe_ || !remaining(deadline)){close();return false;}
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event){close();return false;}
    OVERLAPPED operation{};operation.hEvent=event.get();DWORD bytes=0;
    bool ok=(write?WriteFile(pipe_.get(),data,size,&bytes,&operation):ReadFile(pipe_.get(),data,size,&bytes,&operation))!=FALSE;
    if(!ok && GetLastError()==ERROR_IO_PENDING)ok=completeIo(pipe_.get(),operation,deadline,bytes);
    if(!ok || bytes!=size){close();return false;}return true;
}
bool LocalChannel::send(const Message& message,Deadline deadline){
    auto bytes=encodeMessage(message);if(!bytes){close();return false;}return transfer(true,bytes->data(),static_cast<DWORD>(bytes->size()),deadline);
}
std::optional<Message> LocalChannel::receive(Deadline deadline){
    EncodedMessage bytes{};if(!transfer(false,bytes.data(),static_cast<DWORD>(bytes.size()),deadline))return {};
    auto message=decodeMessage(bytes.data(),bytes.size());if(!message)close();return message;
}
}
