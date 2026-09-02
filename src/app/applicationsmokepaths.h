#pragma once

#include <QString>

#include <functional>

struct ApplicationSmokeStoragePaths final {
    QString appConfigDirectory;
    QString userDataDirectory;
    QString legacyUserSettingsDirectory;
    QString error;
    bool overridden = false;
};

struct ApplicationSmokeRequest final {
    bool requested = false;
    int deadlineMs = 0;
    QString mode;
};

struct ApplicationSmokeStartupPlan final {
    ApplicationSmokeRequest smokeRequest;
    bool createUiRuntime = false;
};

int validatedApplicationSmokeDeadlineMs( const QString& value );
bool isManualIsolationSmokeMode( const QString& mode );
bool shouldShowStorageBootstrapFailureDialog( bool smokeRequested, const QString& smokeMode );
ApplicationSmokeStartupPlan planApplicationSmokeStartup(
    const QString& deadlineValue, const QString& mode, bool uiRuntimeAvailable );

using ApplicationStoragePathsProvider = std::function<ApplicationSmokeStoragePaths()>;

ApplicationSmokeStoragePaths resolveApplicationSmokeStoragePaths(
    bool smokeRequested, const QString& requestedAppConfigDirectory,
    const QString& requestedUserDataDirectory,
    const ApplicationStoragePathsProvider& productionPathsProvider );
