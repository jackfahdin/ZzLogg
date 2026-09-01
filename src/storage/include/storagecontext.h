#pragma once

#include <QString>

enum class StorageMode { UserDirectory, ProgramDirectory, CustomDirectory };

struct StorageLocation {
    StorageMode mode = StorageMode::UserDirectory;
    QString dataRoot;
    QString locatorPath;
    bool commandLineOverride = false;
};

class StorageContext final {
  public:
    explicit StorageContext( StorageLocation location );

    static bool install( StorageLocation location, QString* error = nullptr );
    static bool isInstalled();
    static const StorageContext& current();

    const StorageLocation& location() const;
    QString dataRoot() const;
    QString configDirectory() const;
    QString sessionDirectory() const;
    QString logsDirectory() const;
    QString crashesDirectory() const;
    QString configFilePath() const;
    QString sessionFilePath() const;
    QString manifestFilePath() const;
    bool ensureDirectories( QString* error = nullptr ) const;

  private:
    StorageLocation location_;
};
