#include "zzlogg/updateqt/osversion.h"

#include <QOperatingSystemVersion>

namespace zzlogg::updateqt {

update::OsVersion makeOsVersion(int major, int minor, int micro)
{
    // 构建号为 0 时 patch 不是真实 Windows 构建号，须失败关闭而非部分填充。
    if (major <= 0 || minor < 0 || micro <= 0) return {};
    return {static_cast<std::uint32_t>(major), static_cast<std::uint32_t>(minor),
            static_cast<std::uint32_t>(micro)};
}

update::OsVersion currentOsVersion()
{
#ifdef Q_OS_WIN
    const auto current = QOperatingSystemVersion::current();
    return makeOsVersion(current.majorVersion(), current.minorVersion(), current.microVersion());
#else
    return {};
#endif
}

} // namespace zzlogg::updateqt
