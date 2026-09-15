#include "zzlogg/updateqt/installationidentity.h"

namespace zzlogg::updateqt {

InstallationIdentity evaluateInstallation(const InstallationEvidence& evidence)
{
    if (!evidence.supportedPlatform) return {InstallationKind::Unsupported, {}};
    if (evidence.readFailed) return {};
    if (!evidence.registrationPresent)
        return {evidence.markerPresent ? InstallationKind::Invalid : InstallationKind::Unregistered, {}};
    if (!evidence.safePaths || !evidence.sameDirectory || !evidence.markerPresent
        || !evidence.markerValid) return {};
    if (!evidence.identitySchema) return {InstallationKind::Legacy, {}};
    if (*evidence.identitySchema != 1 || evidence.installRoot.isEmpty()) return {};
    return {InstallationKind::Registered, evidence.installRoot};
}

} // namespace zzlogg::updateqt
