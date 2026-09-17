#include "handoffprotocol.h"
#include "zzlogg/update/packageverification.h"
#include "zzlogg/update/releaseidentity.h"
#include <algorithm>
#include <iostream>
#include <vector>
using namespace zzlogg::updater;
namespace {
int failures=0;
void check(bool value,const char* name) { if(!value) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } }
TransactionId transaction() { TransactionId id{}; id[0]=0x11; id[15]=0x22; return id; }
SessionToken token() { SessionToken value{}; value[0]=0x33; value[31]=0x44; return value; }
Message message(MessageKind kind) { return {kind,transaction(),token()}; }
HandoffSession atStage(int stage) {
    HandoffSession session(transaction(),token());
    constexpr MessageKind sequence[]={MessageKind::Hello,MessageKind::AwaitingAppExit,MessageKind::CommitExit,
        MessageKind::Proceed,MessageKind::Complete};
    for(int i=0;i<stage;++i) session.accept(message(sequence[i]));
    return session;
}
}
int main() {
    // Removing exact framing/field checks would admit corrupted or unauthorized control messages.
    const EncodedMessage fixture={0x5a,0x5a,0x55,0x50, 2,0,0,0, 1,0,0,0,
        0x11,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x22,
        0x33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x44};
    const auto encoded=encodeMessage(message(MessageKind::Hello));
    check(encoded && *encoded==fixture,"literal wire encoding");
    for(uint32_t kind=1;kind<=8;++kind) {
        auto bytes=fixture; bytes[8]=static_cast<uint8_t>(kind);
        const auto decoded=decodeMessage(bytes.data(),bytes.size());
        check(decoded && decoded->kind==static_cast<MessageKind>(kind)
            && decoded->transaction==transaction() && decoded->token==token(),"all control kinds decode");
    }
    for(std::size_t length=0;length<messageSize;++length)
        check(!decodeMessage(fixture.data(),length),"every truncated frame rejected");
    auto trailing=std::vector<uint8_t>(fixture.begin(),fixture.end()); trailing.push_back(0);
    check(!decodeMessage(trailing.data(),trailing.size()),"trailing byte rejected");
    check(!decodeMessage(nullptr,messageSize),"null frame rejected");
    for(auto offset:{0,1,2,3,4,5,6,7,9,10,11}) {
        auto bytes=fixture; bytes[static_cast<std::size_t>(offset)]^=0x80;
        check(!decodeMessage(bytes.data(),bytes.size()),"bad magic version or high kind byte rejected");
    }
    // The retired protocol version 1 framing must never decode again.
    auto retired=fixture; retired[4]=1;
    check(!decodeMessage(retired.data(),retired.size()),"retired protocol version 1 rejected");
    for(auto kind:{0U,9U,255U}) {
        auto bytes=fixture; bytes[8]=static_cast<uint8_t>(kind);
        check(!decodeMessage(bytes.data(),bytes.size()),"unknown kind rejected");
        check(!encodeMessage(message(static_cast<MessageKind>(kind))),"unknown kind not encoded");
    }
    for(bool zeroTransaction:{false,true}) {
        auto invalid=message(MessageKind::Hello);
        auto bytes=fixture;
        if(zeroTransaction) { invalid.transaction={}; std::fill(bytes.begin()+12,bytes.begin()+28,0); }
        else { invalid.token={}; std::fill(bytes.begin()+28,bytes.end(),0); }
        check(!encodeMessage(invalid),"zero identity not encoded");
        check(!decodeMessage(bytes.data(),bytes.size()),"zero identity not decoded");
        HandoffSession invalidSession(invalid.transaction,invalid.token);
        check(!invalidSession.accept(invalid) && invalidSession.state()==HandoffState::Aborted
            && !invalidSession.canExit(),"zero session cannot authorize exit");
    }
    // Table independently specifies the only permitted transition at each phase.
    // Proceed (8) alone lifts ExitCommitted to ExitConfirmed; Complete is valid
    // only after Proceed, never before it.
    constexpr HandoffState states[]={HandoffState::Connecting,HandoffState::Ready,
        HandoffState::AwaitingAppExit,HandoffState::ExitCommitted,HandoffState::ExitConfirmed,HandoffState::Complete};
    constexpr uint32_t expected[]={1,3,4,8,7,0};
    for(int stage=0;stage<=5;++stage) {
        auto session=atStage(stage);
        check(session.state()==states[stage],"ordered handshake state");
        check(session.canExit()==(stage==2 || stage==3),"exit gated by explicit await/commit");
        for(uint32_t kind=0;kind<=9;++kind) {
            auto test=atStage(stage);
            const bool accepted=test.accept(message(static_cast<MessageKind>(kind)));
            const bool cancel=kind==5 || kind==6;
            const bool allowed=kind==expected[stage] && stage<5;
            check(accepted==(allowed || (cancel && stage<5)),"sequence transition acceptance");
            if(!allowed) check(test.state()==HandoffState::Aborted && !test.canExit(),"wrong sequence/cancel/terminal replay aborts");
        }
        for(bool otherTransaction:{false,true}) {
            auto test=atStage(stage); auto wrong=message(stage<5?static_cast<MessageKind>(expected[stage]):MessageKind::Cancel);
            if(otherTransaction) wrong.transaction[7]=1; else wrong.token[7]=1;
            check(!test.accept(wrong) && test.state()==HandoffState::Aborted && !test.canExit(),"foreign identity aborts every phase");
        }
    }
    for(auto terminal:{MessageKind::Cancel,MessageKind::Failed}) for(uint32_t replay=1;replay<=8;++replay) {
        auto session=atStage(0); session.accept(message(terminal));
        check(!session.accept(message(static_cast<MessageKind>(replay)))
            && session.state()==HandoffState::Aborted && !session.canExit(),"aborted terminal rejects every replay");
    }
    // These calls force real execution -> core -> signature/JSON/monocypher linkage.
    auto result=zzlogg::update::verifyPackageForExecution({}, {}, {}, L"C:\\unused.exe");
    check(!result.package,"execution validation linked and rejects empty selection");
    const auto identity=zzlogg::update::compiledReleaseIdentity();
    check(!identity || identity->updaterProtocol>0,"compiled release identity linked");
    std::cout<<"protocol failures: "<<failures<<'\n';
    return failures?1:0;
}
