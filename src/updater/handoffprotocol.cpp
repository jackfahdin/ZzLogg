#include "handoffprotocol.h"
#include <algorithm>
namespace zzlogg::updater {
namespace {
template<class T> bool nonzero(const T& bytes) {
    return std::any_of(bytes.begin(),bytes.end(),[](uint8_t value){return value!=0;});
}
bool valid(const Message& message) {
    const auto kind=static_cast<uint32_t>(message.kind);
    return kind>=1 && kind<=7 && nonzero(message.transaction) && nonzero(message.token);
}
}
std::optional<EncodedMessage> encodeMessage(const Message& message) {
    if(!valid(message)) return {};
    EncodedMessage bytes{0x5a,0x5a,0x55,0x50,1,0,0,0};
    bytes[8]=static_cast<uint8_t>(message.kind);
    std::copy(message.transaction.begin(),message.transaction.end(),bytes.begin()+12);
    std::copy(message.token.begin(),message.token.end(),bytes.begin()+28);
    return bytes;
}
std::optional<Message> decodeMessage(const uint8_t* bytes,std::size_t size) {
    if(!bytes || size!=messageSize) return {};
    constexpr std::array<uint8_t,8> header{0x5a,0x5a,0x55,0x50,1,0,0,0};
    if(!std::equal(header.begin(),header.end(),bytes) || bytes[9] || bytes[10] || bytes[11]) return {};
    Message message{static_cast<MessageKind>(bytes[8]),{}, {}};
    std::copy_n(bytes+12,message.transaction.size(),message.transaction.begin());
    std::copy_n(bytes+28,message.token.size(),message.token.begin());
    return valid(message)?std::optional<Message>(message):std::nullopt;
}
HandoffSession::HandoffSession(TransactionId transaction,SessionToken token)
    :transaction_(transaction),token_(token) {
    if(!nonzero(transaction_) || !nonzero(token_)) state_=HandoffState::Aborted;
}
bool HandoffSession::accept(const Message& message) {
    const auto previous=state_;
    state_=HandoffState::Aborted;
    if(previous==HandoffState::Complete || previous==HandoffState::Aborted || !valid(message)
        || message.transaction!=transaction_ || message.token!=token_) return false;
    if(message.kind==MessageKind::Cancel || message.kind==MessageKind::Failed) return true;
    if(previous==HandoffState::Connecting && message.kind==MessageKind::Hello) state_=HandoffState::Ready;
    else if(previous==HandoffState::Ready && message.kind==MessageKind::AwaitingAppExit) state_=HandoffState::AwaitingAppExit;
    else if(previous==HandoffState::AwaitingAppExit && message.kind==MessageKind::CommitExit) state_=HandoffState::ExitCommitted;
    else if(previous==HandoffState::ExitCommitted && message.kind==MessageKind::Complete) state_=HandoffState::Complete;
    return state_!=HandoffState::Aborted;
}
bool HandoffSession::canExit() const {
    return state_==HandoffState::AwaitingAppExit || state_==HandoffState::ExitCommitted;
}
}
