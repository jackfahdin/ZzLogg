#include "zzlogg/updateqt/updateservice.h"

// Link-time fixture for the application menu integration test only. This
// satisfies the application's configuration entry point before the static
// library is searched, keeping the test offline for production and local feeds.
// It is never linked into ZzLogg or the production configuration tests.
namespace zzlogg::updateqt {
FeedConfiguration appFeedConfiguration()
{
    return {};
}
}
