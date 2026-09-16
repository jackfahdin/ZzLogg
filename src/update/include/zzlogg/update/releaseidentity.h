#pragma once

#include "zzlogg/update/version.h"

#include <cstdint>
#include <optional>
#include <string>

namespace zzlogg::update {
struct ReleaseIdentity {
    Version version{};
    std::uint64_t releaseSequence = 0;
    std::string channel;
    std::string os;
    std::string arch;
    std::uint32_t dataSchema = 0;
    std::uint32_t updaterProtocol = 1;
};

std::optional<ReleaseIdentity> compiledReleaseIdentity();
}
