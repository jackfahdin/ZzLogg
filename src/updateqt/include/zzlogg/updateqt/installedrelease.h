#pragma once

#include "zzlogg/update/policy.h"
#include "zzlogg/update/releaseidentity.h"
#include "zzlogg/updateqt/installationidentity.h"

#include <optional>

namespace zzlogg::updateqt {

std::optional<update::InstalledRelease> makeInstalledRelease(
    const std::optional<update::ReleaseIdentity>& release,
    const InstallationIdentity& installation,
    const update::OsVersion& osVersion);

} // namespace zzlogg::updateqt
