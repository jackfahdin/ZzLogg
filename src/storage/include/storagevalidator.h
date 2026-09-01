#pragma once

#include "storagecontext.h"

#include <QString>

struct StorageValidationResult {
    bool valid = false;
    bool managedDirectory = false;
    QString normalizedRoot;
    QString error;
};

class StorageValidator final {
  public:
    static StorageValidationResult validate( const QString& root,
                                             bool allowExistingManagedDirectory );
    static bool writeManifest( const StorageContext& context, QString* error = nullptr );
    static bool hasCompatibleManifest( const QString& root );
};
