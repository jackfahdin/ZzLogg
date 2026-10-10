#pragma once

#include <QByteArrayView>
#include <QSaveFile>
#include <QString>
#include <memory>
#include <optional>
#include "zzlogg/update/manifest.h"

namespace zzlogg::updateqt {
enum class CacheError {
    None,
    InvalidPath,
    Busy,
    InvalidArtifact,
    InsufficientSpace,
    WriteFailed,
    SizeMismatch,
    HashMismatch,
    Cancelled
};

class PackageCacheStorage {
public:
    virtual ~PackageCacheStorage() = default;
    virtual std::optional<quint64> availableBytes(const QString& path) const = 0;
    virtual std::unique_ptr<QSaveFile> createSaveFile(const QString& path) const = 0;
};

class PackageCache {
public:
    PackageCache(QString cacheRoot, update::Artifact artifact);
    PackageCache(QString cacheRoot, update::Artifact artifact,
                 std::shared_ptr<const PackageCacheStorage> storage);
    ~PackageCache();

    PackageCache(const PackageCache&) = delete;
    PackageCache& operator=(const PackageCache&) = delete;

    CacheError begin();
    CacheError append(QByteArrayView bytes);
    CacheError finish();
    CacheError cancel();
    QString verifiedPath() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Cache file name for an artifact: sha256 hex plus a real payload extension.
// Installer packages must end in .exe — the update handoff launches the
// verified package through ShellExecuteEx "runas", which requires an
// executable association (a neutral extension fails with
// ERROR_NO_ASSOCIATION before any UAC prompt).
QString packageCacheFileName(const update::Artifact& artifact);
}
