#include "applicationsmokepaths.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString normalizedSafeDirectory( const QString& path )
{
    const QString normalized = QDir::cleanPath( QDir::fromNativeSeparators( path ) );
    if ( path.isEmpty() || !QFileInfo{ normalized }.isAbsolute() || QDir{ normalized }.isRoot() ) {
        return {};
    }
    return normalized;
}

} // namespace

int validatedApplicationSmokeDeadlineMs( const QString& value )
{
    bool valid = false;
    const int deadlineMs = value.toInt( &valid );
    return valid && deadlineMs > 0 ? deadlineMs : 0;
}

ApplicationSmokeStoragePaths resolveApplicationSmokeStoragePaths(
    bool smokeRequested, const QString& requestedAppConfigDirectory,
    const QString& requestedUserDataDirectory,
    const ApplicationStoragePathsProvider& productionPathsProvider )
{
    if ( smokeRequested ) {
        const QString appConfigDirectory
            = normalizedSafeDirectory( requestedAppConfigDirectory );
        const QString userDataDirectory = normalizedSafeDirectory( requestedUserDataDirectory );
        if ( !appConfigDirectory.isEmpty() && !userDataDirectory.isEmpty() ) {
            return { appConfigDirectory, userDataDirectory, {}, true };
        }
    }

    auto productionPaths = productionPathsProvider();
    productionPaths.error.clear();
    productionPaths.overridden = false;
    return productionPaths;
}
