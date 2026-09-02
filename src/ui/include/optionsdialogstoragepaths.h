#pragma once

#include <QString>

#include <functional>

struct OptionsDialogStoragePaths {
    QString applicationDirectory;
    QString appConfigDirectory;
    QString userDataDirectory;
};

using OptionsDialogStoragePathsProvider = std::function<OptionsDialogStoragePaths()>;

OptionsDialogStoragePaths resolveOptionsDialogStoragePaths(
    const OptionsDialogStoragePaths& installedPaths,
    const OptionsDialogStoragePaths& testOverridePaths,
    const OptionsDialogStoragePathsProvider& productionPathsProvider );
