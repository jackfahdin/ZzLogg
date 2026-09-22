#include "zzlogg/updateqt/currentinstallation.h"

#include "zzlogg/updateqt/osversion.h"

namespace zzlogg::updateqt {

std::optional<update::InstalledRelease> composeInstalledRelease(
    const std::optional<update::ReleaseIdentity>& release,
    const InstallationIdentity& installation,
    const update::OsVersion& osVersion)
{
    return makeInstalledRelease(release, installation, osVersion);
}

std::optional<update::InstalledRelease> currentInstalledRelease()
{
    return composeInstalledRelease(update::compiledReleaseIdentity(),
                                   probeCurrentInstallation(), currentOsVersion());
}

} // namespace zzlogg::updateqt
