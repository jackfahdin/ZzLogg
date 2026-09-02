#include "optionsdialogstoragepaths.h"

#include <QDir>

namespace {

bool isComplete( const OptionsDialogStoragePaths& paths )
{
    return !paths.applicationDirectory.isEmpty() && !paths.appConfigDirectory.isEmpty()
           && !paths.userDataDirectory.isEmpty();
}

OptionsDialogStoragePaths normalized( OptionsDialogStoragePaths paths )
{
    paths.applicationDirectory
        = QDir::cleanPath( QDir::fromNativeSeparators( paths.applicationDirectory ) );
    paths.appConfigDirectory
        = QDir::cleanPath( QDir::fromNativeSeparators( paths.appConfigDirectory ) );
    paths.userDataDirectory
        = QDir::cleanPath( QDir::fromNativeSeparators( paths.userDataDirectory ) );
    return paths;
}

} // namespace

OptionsDialogStoragePaths resolveOptionsDialogStoragePaths(
    const OptionsDialogStoragePaths& installedPaths,
    const OptionsDialogStoragePaths& testOverridePaths,
    const OptionsDialogStoragePathsProvider& productionPathsProvider )
{
    if ( isComplete( testOverridePaths ) ) {
        return normalized( testOverridePaths );
    }
    if ( isComplete( installedPaths ) ) {
        return normalized( installedPaths );
    }
    return productionPathsProvider ? normalized( productionPathsProvider() )
                                   : OptionsDialogStoragePaths{};
}
