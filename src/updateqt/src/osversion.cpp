#include "zzlogg/updateqt/osversion.h"

#include <QOperatingSystemVersion>

namespace zzlogg::updateqt {

update::OsVersion currentOsVersion()
{
#ifdef Q_OS_WIN
    const auto current = QOperatingSystemVersion::current();
    const auto major = current.majorVersion();
    const auto minor = current.minorVersion();
    const auto micro = current.microVersion();
    if (major <= 0 || minor < 0 || micro < 0) return {};
    return {static_cast<std::uint32_t>(major), static_cast<std::uint32_t>(minor),
            static_cast<std::uint32_t>(micro)};
#else
    return {};
#endif
}

} // namespace zzlogg::updateqt
