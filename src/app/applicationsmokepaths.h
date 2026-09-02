#pragma once

#include <QString>

#include <functional>

struct ApplicationSmokeStoragePaths final {
    QString appConfigDirectory;
    QString userDataDirectory;
    QString error;
    bool overridden = false;
};

int validatedApplicationSmokeDeadlineMs( const QString& value );

using ApplicationStoragePathsProvider = std::function<ApplicationSmokeStoragePaths()>;

ApplicationSmokeStoragePaths resolveApplicationSmokeStoragePaths(
    bool smokeRequested, const QString& requestedAppConfigDirectory,
    const QString& requestedUserDataDirectory,
    const ApplicationStoragePathsProvider& productionPathsProvider );
