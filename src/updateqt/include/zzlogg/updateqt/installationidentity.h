#pragma once

#include <QString>
#include <optional>
#include <functional>

class QObject;

namespace zzlogg::updateqt {

enum class InstallationKind { Unregistered, Legacy, Registered, Invalid, Unsupported, Portable, SystemPackage };

struct InstallationEvidence {
    bool supportedPlatform = false;
    bool readFailed = false;
    bool registrationPresent = false;
    bool safePaths = false;
    bool sameDirectory = false;
    bool markerPresent = false;
    bool markerValid = false;
    bool manifestPresent = false;
    bool manifestValid = false;
    std::optional<unsigned> identitySchema;
    QString installRoot;
};

struct InstallationIdentity {
    InstallationKind kind = InstallationKind::Invalid;
    QString installRoot;
    QString packageFormat;
};

InstallationIdentity evaluateInstallation(const InstallationEvidence& evidence);
InstallationIdentity probeCurrentInstallation();
// Callback runs on context's thread and is discarded if context is destroyed.
void probeCurrentInstallationAsync(QObject* context,
    std::function<void(InstallationIdentity)> callback);

} // namespace zzlogg::updateqt
