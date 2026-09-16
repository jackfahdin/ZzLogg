#include "zzlogg/update/releaseidentity.h"

#include <cstdint>

int main()
{
    const auto release = zzlogg::update::compiledReleaseIdentity();
#if TEST_EXPECT_AVAILABLE
    if (!release || release->version.year != 26 || release->version.month != 9
        || release->version.patch != 0
        || release->releaseSequence != std::uint64_t{TEST_EXPECT_SEQUENCE}
        || release->channel != TEST_EXPECT_CHANNEL || release->os != "windows"
        || release->arch != "x64"
        || release->dataSchema != std::uint32_t{TEST_EXPECT_SCHEMA}
        || release->updaterProtocol != 1) {
        return 1;
    }
#else
    if (release) return 1;
#endif
    return 0;
}
