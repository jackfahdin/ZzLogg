#include "zzlogg/updateqt/updateservice.h"

// Link-time fixture for the application menu integration test only. This
// satisfies the configuration symbol before the static library is searched,
// keeping that test offline even when the real application embeds a feed.
// It is never linked into ZzLogg or the production configuration tests.
namespace zzlogg::updateqt {
FeedConfiguration productionFeedConfiguration()
{
    return {};
}
}
