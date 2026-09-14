#pragma once

#include "storagecontext.h"

#include <QString>

#include <optional>

enum class StorageResolutionSource {
    CommandLine,
    ProgramLocator,
    UserLocator,
    LegacyStorage,
    Missing
};

struct StorageMigrationRequest {
    QString transactionId;
    StorageLocation source;
    StorageLocation target;
    QString legacyConfigFile;
    QString legacySessionFile;
    QString sourceLogsDirectory;
    bool sourceLocatorExisted = true;
};

struct StorageLocatorState {
    int formatVersion = 1;
    StorageLocation active;
    std::optional<StorageMigrationRequest> pending;
    bool verified = false;
};

struct StorageResolution {
    StorageResolutionSource source = StorageResolutionSource::Missing;
    std::optional<StorageLocatorState> state;
    QString error;
};

class StorageLocatorStore final {
public:
    StorageLocatorStore( QString applicationDirectory, QString appConfigDirectory );
    QString programLocatorPath() const;
    QString userLocatorPath() const;
    QString mutationLockPath() const;
    StorageResolution resolve( const QString& commandLineDataRoot = {} ) const;
    bool writeActive( const StorageLocation& location, QString* error = nullptr ) const;
    bool writePending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
    bool commitPending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
    bool rollbackPending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
    bool discardUnverifiedLegacyLocator( const StorageLocation& location,
                                         QString* error = nullptr ) const;

private:
    QString applicationDirectory_;
    QString appConfigDirectory_;
};
