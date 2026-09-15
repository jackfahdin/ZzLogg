#pragma once

#include <QString>
#include <optional>

namespace zzlogg::updateqt {

enum class InstallationKind { Unregistered, Legacy, Registered, Invalid, Unsupported };

struct InstallationEvidence {
    bool supportedPlatform = false;
    bool readFailed = false;
    bool registrationPresent = false;
    bool safePaths = false;
    bool sameDirectory = false;
    bool markerPresent = false;
    bool markerValid = false;
    std::optional<unsigned> identitySchema;
    QString installRoot;
};

struct InstallationIdentity {
    InstallationKind kind = InstallationKind::Invalid;
    QString installRoot;
};

InstallationIdentity evaluateInstallation(const InstallationEvidence& evidence);
InstallationIdentity probeCurrentInstallation();

} // namespace zzlogg::updateqt
