#pragma once

#include "storagelocator.h"

#include <QString>

#include <functional>

struct StorageMigrationResult {
    bool success = false;
    bool rolledBack = false;
    QString error;
};

using StorageCopyOperation
    = std::function<bool( const QString& source, const QString& target, QString* error )>;

class StorageMigrator final {
public:
    explicit StorageMigrator( StorageLocatorStore locatorStore,
                              StorageCopyOperation copyOperation = {} );
    StorageMigrationResult execute( const StorageMigrationRequest& request ) const;
    StorageMigrationResult recoverPending( const StorageMigrationRequest& request ) const;

private:
    StorageLocatorStore locatorStore_;
    StorageCopyOperation copyOperation_;
};
