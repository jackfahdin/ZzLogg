#include "zzlogg/updateqt/installedrelease.h"

#include <array>
#include <string_view>

namespace zzlogg::updateqt {
namespace {

bool isValidVersion(const update::Version& version)
{
    if (version.year > 99 || version.month > 99 || version.patch > 99) return false;
    const std::array<char, 8> text{
        char('0' + version.year / 10), char('0' + version.year % 10), '.',
        char('0' + version.month / 10), char('0' + version.month % 10), '.',
        char('0' + version.patch / 10), char('0' + version.patch % 10),
    };
    return update::parseVersion(std::string_view{text.data(), text.size()}).has_value();
}

} // namespace

std::optional<update::InstalledRelease> makeInstalledRelease(
    const std::optional<update::ReleaseIdentity>& release,
    const InstallationIdentity& installation,
    const update::OsVersion& osVersion)
{
    if (!release || installation.kind != InstallationKind::Registered
        || installation.installRoot.isEmpty() || !isValidVersion(release->version)
        || release->releaseSequence == 0
        || (release->channel != "stable" && release->channel != "preview")
        || release->os != "windows" || release->arch != "x64"
        || release->updaterProtocol != 1 || osVersion.major == 0) {
        return std::nullopt;
    }

    update::InstalledRelease result;
    result.version = release->version;
    result.releaseSequence = release->releaseSequence;
    result.developmentBuild = false;
    result.channel = release->channel;
    result.os = release->os;
    result.arch = release->arch;
    result.distribution = update::Distribution::Installer;
    result.osVersion = osVersion;
    result.dataSchema = release->dataSchema;
    result.updaterProtocol = release->updaterProtocol;
    return result;
}

} // namespace zzlogg::updateqt
