#pragma once

#include "storagecontext.h"

#include <QString>

#include <functional>
#include <optional>

struct StorageBootstrapPrompt {
    QString applicationDirectory;
    QString userDataDirectory;
};

using StorageSelectionProvider
    = std::function<std::optional<StorageLocation>( const StorageBootstrapPrompt& )>;

enum class StorageBootstrapStatus { Ready, Cancelled, Error };

struct StorageBootstrapResult {
    StorageBootstrapStatus status = StorageBootstrapStatus::Error;
    QString error;
};

StorageBootstrapResult
bootstrapStorage( const QString& applicationDirectory, const QString& appConfigDirectory,
                  const QString& userDataDirectory,
                  const QString& legacyUserSettingsDirectory,
                  const QString& commandLineDataRoot,
                  StorageSelectionProvider selectionProvider );
