#pragma once
#include "zzlogg/update/manifest.h"
#include <QString>
#include <functional>
#include <optional>
namespace zzlogg::updateqt {
enum class Channel { Stable, Preview };
struct ChannelState {
    std::optional<update::AcceptedMetadata> accepted;
    qint64 lastSuccess=0, nextAttempt=0;
    unsigned failureCount=0;
    std::optional<std::uint64_t> skippedReleaseSequence;
};
enum class StateError { None, Invalid, Busy, ReadFailed, WriteFailed, Replay, Conflict };
struct StateResult {
    std::optional<ChannelState> value;
    StateError error=StateError::Invalid;
};
bool isCheckDue(const ChannelState&, qint64 now);
class UpdateStateStore {
public:
    explicit UpdateStateStore(QString absoluteFile);
    StateResult read(Channel) const;
    StateError accept(Channel, const update::AcceptedMetadata&, qint64 now);
    StateError recordFailure(Channel, qint64 now);
    StateError recordCancellation(Channel, qint64 now);
    StateError skip(Channel, std::uint64_t releaseSequence);
private:
    StateError modify(Channel, const std::function<StateError(ChannelState&)>&);
    QString file_;
};
}
