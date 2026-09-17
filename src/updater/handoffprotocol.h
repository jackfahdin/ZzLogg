#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace zzlogg::updater {
enum class MessageKind : uint32_t { Hello=1, Ready=2, AwaitingAppExit=3,
    CommitExit=4, Cancel=5, Failed=6, Complete=7, Proceed=8 };
using TransactionId = std::array<uint8_t,16>;
using SessionToken = std::array<uint8_t,32>;
struct Message { MessageKind kind; TransactionId transaction; SessionToken token; };
enum class HandoffState { Connecting, Ready, AwaitingAppExit, ExitCommitted, ExitConfirmed, Complete, Aborted };
// Control-only wire format: "ZZUP", little-endian uint32 version (2), kind,
// 16 transaction bytes, 32 token bytes. Never carries installation authority.
// ExitCommitted lifts to ExitConfirmed only through Proceed, which the
// coordinator sends after the application's real process exit; Complete is
// valid only from ExitConfirmed, so no file modification may be reported
// before the gate.
inline constexpr std::size_t messageSize=60;
using EncodedMessage=std::array<uint8_t,messageSize>;
std::optional<EncodedMessage> encodeMessage(const Message&);
std::optional<Message> decodeMessage(const uint8_t*,std::size_t);
class HandoffSession {
public:
    HandoffSession(TransactionId,SessionToken);
    bool accept(const Message&);
    HandoffState state() const { return state_; }
    bool canExit() const;
private:
    TransactionId transaction_;
    SessionToken token_;
    HandoffState state_=HandoffState::Connecting;
};
}
