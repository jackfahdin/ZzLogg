#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg_update_feed.h"
#include <QStringList>
#include <optional>
namespace zzlogg::updateqt {
namespace {
// Runtime opt-in local feed for update-loop development. Only reachable in
// builds compiled with ZZLOGG_UPDATE_LOCAL_FEED; both the URL and a Test-purpose
// key must be supplied through the environment, so a shipped binary can never
// be silently redirected: production binaries lack the capability entirely.
std::optional<FeedConfiguration> localFeedConfigurationFromEnvironment()
{
#ifdef ZZLOGG_UPDATE_LOCAL_FEED
    const auto url=qEnvironmentVariable("ZZLOGG_UPDATE_FEED_URL");
    const auto key=qEnvironmentVariable("ZZLOGG_UPDATE_FEED_KEY");
    if(url.isEmpty() || key.isEmpty()) return std::nullopt;
    const auto colon=key.indexOf(':');
    const auto bytes=QByteArray::fromHex(key.mid(colon+1).toLatin1());
    if(colon<1 || bytes.size()!=32) return std::nullopt;
    FeedConfiguration result;
    result.environment=update::TrustEnvironment::Test;
    result.stableUrl=url;
    const auto preview=qEnvironmentVariable("ZZLOGG_UPDATE_FEED_PREVIEW_URL");
    result.previewUrl=preview.isEmpty() ? url : preview;
    result.buildTime=0;
    const auto hosts=qEnvironmentVariable("ZZLOGG_UPDATE_FEED_HOSTS",
        QStringLiteral("127.0.0.1;localhost"));
    for (const auto& host:hosts.split(';',Qt::SkipEmptyParts))
        result.allowedHosts.push_back(host.toStdString());
    result.keys.push_back({key.left(colon).toStdString(),
        {bytes.begin(),bytes.end()},update::KeyPurpose::Test});
    return result;
#else
    return std::nullopt;
#endif
}
}
FeedConfiguration productionFeedConfiguration()
{
    auto decode=[](const char* value){ return QString::fromUtf8(QByteArray::fromHex(value)); };
    FeedConfiguration result;
    result.stableUrl=decode(build::Stable);
    result.previewUrl=decode(build::Preview);
    result.buildTime=build::Time;
    for (const auto& host:decode(build::Hosts).split(';',Qt::SkipEmptyParts))
        result.allowedHosts.push_back(host.toStdString());
    for (const auto& entry:decode(build::Keys).split(';',Qt::SkipEmptyParts)) {
        const auto colon=entry.indexOf(':');
        const auto bytes=QByteArray::fromHex(entry.mid(colon+1).toLatin1());
        result.keys.push_back({entry.left(colon).toStdString(),
            {bytes.begin(),bytes.end()},update::KeyPurpose::Production});
    }
    return result;
}
FeedConfiguration appFeedConfiguration()
{
    if (const auto local=localFeedConfigurationFromEnvironment()) return *local;
    return productionFeedConfiguration();
}
bool localUpdateFeedRequested()
{
#ifdef ZZLOGG_UPDATE_LOCAL_FEED
    return qEnvironmentVariableIsSet("ZZLOGG_UPDATE_FEED_URL");
#else
    return false;
#endif
}
}
