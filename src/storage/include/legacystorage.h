#pragma once

#include "storagecontext.h"

#include <QString>

#include <optional>

struct LegacyStorage {
    StorageMode mode = StorageMode::UserDirectory;
    QString configFile;
    QString sessionFile;
};

class LegacyStorageDetector final {
public:
    static std::optional<LegacyStorage> detect( const QString& applicationDirectory,
                                                const QString& userSettingsDirectory );
};
