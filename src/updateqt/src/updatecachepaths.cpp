#include "zzlogg/updateqt/updatecachepaths.h"
#include <QDir>
#include <QStandardPaths>
namespace zzlogg::updateqt {
QString updateCachePath(update::TrustEnvironment environment)
{
    const bool test=environment==update::TrustEnvironment::Test;
    if(environment!=update::TrustEnvironment::Production && !test) return {};
    // A test process must never derive the production cache, or vice versa.
    if(test!=QStandardPaths::isTestModeEnabled()) return {};
    const auto root=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if(root.isEmpty() || !QDir::isAbsolutePath(root)) return {};
    return QDir(root).filePath(test ? QStringLiteral("updates/test/packages-v1")
                                  : QStringLiteral("updates/production/packages-v1"));
}
}
