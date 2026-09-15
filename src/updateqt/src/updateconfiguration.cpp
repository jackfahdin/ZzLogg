#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg_update_feed.h"
#include <QStringList>
namespace zzlogg::updateqt {
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
}
