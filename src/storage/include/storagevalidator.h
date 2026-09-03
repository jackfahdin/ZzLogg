#pragma once

#include "storagecontext.h"

#include <QString>
#include <QStringList>

enum class StorageValidationError {
    None,
    InvalidRoot,
    NotDirectory,
    CannotCreateDirectory,
    CannotWriteDirectory,
    CannotReadDirectory,
    CannotAtomicallyWriteDirectory,
    CannotReadAtomicWrite,
    CannotRemoveProbe,
    NotEmptyManagedDirectory,
};

struct StorageValidationResult {
    bool valid = false;
    bool managedDirectory = false;
    QString normalizedRoot;
    StorageValidationError errorCode = StorageValidationError::None;
    QStringList errorParameters;
    QString error;
};

class StorageValidator final {
  public:
    static StorageValidationResult validate( const QString& root,
                                             bool allowExistingManagedDirectory );
    static bool writeManifest( const StorageContext& context, QString* error = nullptr );
    static bool hasCompatibleManifest( const QString& root );
};
