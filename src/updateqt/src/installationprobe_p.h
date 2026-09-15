#pragma once

#include "zzlogg/updateqt/installationidentity.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <functional>
#endif

namespace zzlogg::updateqt {

struct InstallationRegistration {
    bool present = false;
    bool readFailed = false;
    QString location;
    std::optional<unsigned> schema;
};

class InstallationRegistryReader {
public:
    virtual ~InstallationRegistryReader() = default;
    virtual InstallationRegistration readMachine64() const = 0;
};

InstallationIdentity probeInstallation(const QString& executablePath,
                                      const InstallationRegistryReader& registry);

#ifdef Q_OS_WIN
// Internal OS boundary: the same bounded query/parser runs in production and tests.
using InstallationValueQuery = std::function<LSTATUS(const wchar_t*, DWORD*, BYTE*, DWORD*)>;
InstallationRegistration readInstallationValues(const InstallationValueQuery& query);
#endif

} // namespace zzlogg::updateqt
