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
    // Schema 1 installations predate the landing manifest: they upgrade only by
    // manually installing a bootstrap version, never automatically.
    if (!evidence.identitySchema || *evidence.identitySchema == 1)
        return {InstallationKind::Legacy, {}};
    if (*evidence.identitySchema != 2 || evidence.installRoot.isEmpty()) return {};
    if (!evidence.manifestPresent || !evidence.manifestValid) return {};
    return {InstallationKind::Registered, evidence.installRoot};
}

} // namespace zzlogg::updateqt
