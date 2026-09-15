#include "zzlogg/updateqt/updatestate.h"
#include "strictjson.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <charconv>
#include <limits>

namespace zzlogg::updateqt {
namespace {
using Json = nlohmann::json;
using States = std::array<ChannelState, 2>;
constexpr qint64 maxBytes = 16 * 1024;
constexpr qint64 day = 86400;

bool validChannel(Channel channel)
{
    return channel == Channel::Stable || channel == Channel::Preview;
}
qint64 deadline(qint64 now, qint64 delay)
{
    return now > (std::numeric_limits<qint64>::max)() - delay
        ? (std::numeric_limits<qint64>::max)() : now + delay;
}
bool nonnegative(const Json& value, qint64& result)
{
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > std::uint64_t((std::numeric_limits<qint64>::max)())) return false;
        result = static_cast<qint64>(number);
    } else {
        result = value.get<qint64>();
    }
    return result >= 0;
}
bool sequence(const Json& value, std::uint64_t& result)
{
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text[0] < '1' || text[0] > '9') return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool parseChannel(const Json& json, ChannelState& state)
{
    if (!json.is_object() || json.size() != 5 || !json.contains("accepted")
        || !json.contains("lastSuccess") || !json.contains("nextAttempt")
        || !json.contains("failureCount") || !json.contains("skippedReleaseSequence")) return false;
    qint64 failures;
    if (!nonnegative(json.at("lastSuccess"), state.lastSuccess)
        || !nonnegative(json.at("nextAttempt"), state.nextAttempt)
        || !nonnegative(json.at("failureCount"), failures) || failures > 4) return false;
    state.failureCount = static_cast<unsigned>(failures);
    const auto& accepted = json.at("accepted");
    if (!accepted.is_null()) {
        update::AcceptedMetadata record;
        if (!accepted.is_object() || accepted.size() != 2 || !accepted.contains("sequence")
            || !accepted.contains("payloadDigest") || !sequence(accepted.at("sequence"), record.sequence)
            || !accepted.at("payloadDigest").is_string()) return false;
        const auto& digest = accepted.at("payloadDigest").get_ref<const std::string&>();
        if (digest.size() != 128) return false;
        for (const char ch : digest)
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
        const auto decoded = QByteArray::fromHex(QByteArray::fromStdString(digest));
        std::copy(decoded.begin(), decoded.end(), record.payloadDigest.begin());
        state.accepted = record;
    }
    if (!json.at("skippedReleaseSequence").is_null()) {
        std::uint64_t skipped;
        if (!sequence(json.at("skippedReleaseSequence"), skipped)) return false;
        state.skippedReleaseSequence = skipped;
    }
    return true;
}
StateError load(const QString& path, States& states)
{
    const QFileInfo info(path);
    if (!info.exists()) return info.isSymLink() ? StateError::ReadFailed : StateError::None;
    if (!info.isFile()) return StateError::ReadFailed;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return StateError::ReadFailed;
    const auto bytes = file.read(maxBytes + 1);
    if (file.error() != QFileDevice::NoError) return StateError::ReadFailed;
    const auto json = update::detail::parseStrictJson(bytes.toStdString(), maxBytes);
    if (!json || !json->is_object() || json->size() != 3 || !json->contains("schema")
        || !json->contains("stable") || !json->contains("preview")) return StateError::Invalid;
    qint64 schema;
    if (!nonnegative(json->at("schema"), schema) || schema != 1
        || !parseChannel(json->at("stable"), states[0])
        || !parseChannel(json->at("preview"), states[1])) return StateError::Invalid;
    return StateError::None;
}
Json serialize(const ChannelState& state)
{
    Json json{{"accepted", nullptr}, {"lastSuccess", state.lastSuccess},
        {"nextAttempt", state.nextAttempt}, {"failureCount", state.failureCount},
        {"skippedReleaseSequence", nullptr}};
    if (state.accepted) {
        const auto& digest = state.accepted->payloadDigest;
        json["accepted"] = {{"sequence", std::to_string(state.accepted->sequence)},
            {"payloadDigest", QByteArray(reinterpret_cast<const char*>(digest.data()),
                static_cast<qsizetype>(digest.size())).toHex().toStdString()}};
    }
    if (state.skippedReleaseSequence)
        json["skippedReleaseSequence"] = std::to_string(*state.skippedReleaseSequence);
    return json;
}
}

UpdateStateStore::UpdateStateStore(QString path) : file_(std::move(path)) {}

StateResult UpdateStateStore::read(Channel channel) const
{
    if (!validChannel(channel) || !QDir::isAbsolutePath(file_)) return {{}, StateError::Invalid};
    States states;
    const auto error = load(file_, states);
    if (error != StateError::None) return {{}, error};
    return {states[static_cast<size_t>(channel)], StateError::None};
}

StateError UpdateStateStore::modify(Channel channel,
    const std::function<StateError(ChannelState&)>& mutation)
{
    if (!validChannel(channel) || !QDir::isAbsolutePath(file_)) return StateError::Invalid;
    if (!QDir().mkpath(QFileInfo(file_).absolutePath())) return StateError::WriteFailed;
    QLockFile lock(file_ + ".lock");
    if (!lock.tryLock(0)) return lock.error() == QLockFile::LockFailedError
        ? StateError::Busy : StateError::WriteFailed;
    // Reload under the lock: another process may have accepted a newer manifest.
    States states;
    const auto error = load(file_, states);
    if (error != StateError::None) return error;
    const auto changed = mutation(states[static_cast<size_t>(channel)]);
    if (changed != StateError::None) return changed;
    const Json json{{"schema", 1}, {"stable", serialize(states[0])}, {"preview", serialize(states[1])}};
    const auto bytes = QByteArray::fromStdString(json.dump());
    QSaveFile file(file_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return StateError::WriteFailed;
    return StateError::None;
}

StateError UpdateStateStore::accept(Channel channel, const update::AcceptedMetadata& record, qint64 now)
{
    if (!record.sequence || now < 0) return StateError::Invalid;
    return modify(channel, [&](ChannelState& state) {
        if (state.accepted) {
            if (record.sequence < state.accepted->sequence) return StateError::Replay;
            if (record.sequence == state.accepted->sequence
                && record.payloadDigest != state.accepted->payloadDigest) return StateError::Conflict;
        }
        state.accepted = record;
        state.lastSuccess = now;
        state.nextAttempt = deadline(now, day);
        state.failureCount = 0;
        return StateError::None;
    });
}
StateError UpdateStateStore::recordFailure(Channel channel, qint64 now)
{
    if (now < 0) return StateError::Invalid;
    return modify(channel, [&](ChannelState& state) {
        constexpr qint64 delays[]{900, 3600, 21600, day};
        if (state.failureCount < 4) ++state.failureCount;
        state.nextAttempt = deadline(now, delays[state.failureCount - 1]);
        return StateError::None;
    });
}
StateError UpdateStateStore::recordCancellation(Channel channel, qint64 now)
{
    if (now < 0) return StateError::Invalid;
    return modify(channel, [&](ChannelState& state) {
        state.nextAttempt = deadline(now, 900);
        return StateError::None;
    });
}
StateError UpdateStateStore::skip(Channel channel, std::uint64_t releaseSequence)
{
    if (!releaseSequence) return StateError::Invalid;
    return modify(channel, [&](ChannelState& state) {
        state.skippedReleaseSequence = releaseSequence;
        return StateError::None;
    });
}
bool isCheckDue(const ChannelState& state, qint64 now)
{
    return now >= 0 && (now >= state.nextAttempt || state.nextAttempt > deadline(now, day));
}
}
