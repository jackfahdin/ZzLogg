#include "zzlogg/update/releaseidentity.h"

#include "zzlogg_release_identity.h"

namespace zzlogg::update {
std::optional<ReleaseIdentity> compiledReleaseIdentity()
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!build_identity::available) return std::nullopt;

    const auto version = parseVersion(build_identity::version);
    if (!version) return std::nullopt;
    return ReleaseIdentity{*version,
                           build_identity::releaseSequence,
                           build_identity::channel,
                           build_identity::os,
                           build_identity::arch,
                           build_identity::dataSchema,
                           build_identity::updaterProtocol};
#else
    return std::nullopt;
#endif
}
}
