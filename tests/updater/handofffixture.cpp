#include "bootstrap_win_p.h"
#include <string>
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
// All alternative behavior belongs to this non-deployed test target only.
int wmain(int argc,wchar_t** argv) {
    ChildBootstrap bootstrap;if(!bootstrap.open(argc,argv))return 51;
    wchar_t modeText[80]{};GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_FIXTURE",modeText,80);std::wstring mode=modeText;
    if(mode==L"early")return 0;
    if(mode==L"timeout"){Sleep(500);return 0;}
    LocalChannel channel;
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),after(3000)))return 52;
    auto message=Message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(mode==L"token")message.token[0]^=1;
    if(mode==L"order")message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000)))return 53;
    auto ready=channel.receive(after(1500));if(!ready || ready->kind!=MessageKind::Ready)return 0;
    if(mode==L"hello-only"){Sleep(400);return 0;}
    if(mode==L"exit-after-hello")return 0;
    message.kind=mode==L"replay"?MessageKind::Hello:mode==L"cancel"?MessageKind::Cancel:MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(1000)))return 54;
    if(mode==L"cancel" || mode==L"replay")return 0;
    if(mode==L"die-waiting")return 0; // Peer dies after AwaitingAppExit, before CommitExit.
    auto commit=channel.receive(after(2000));
    if(mode==L"orphan"){
        // Keep the child alive after the real parent dies so the harness can
        // test self-owned leases after all inherited parent handles vanished.
        if(WaitForSingleObject(bootstrap.parent().handle(),1000)!=WAIT_OBJECT_0)return 55;
        Sleep(600);return 0;
    }
    if(!commit || commit->kind!=MessageKind::CommitExit)return 0;
    message.kind=MessageKind::Complete;channel.send(message,after(1000));
    if(mode==L"linger")Sleep(600);
    if(mode==L"waitgate"){
        // Hold the bootstrap directory observer until the harness opens the
        // named gate, so gate continuity is observable after the parent's
        // real exit. The bounded wait keeps a broken harness from hanging.
        wchar_t name[256]{};GetEnvironmentVariableW(L"ZZLOGG_HANDOFF_GATE",name,256);
        Handle gate(OpenEventW(SYNCHRONIZE,FALSE,name));
        if(!gate)return 9;
        WaitForSingleObject(gate.get(),30000);
    }
    return 0;
}
