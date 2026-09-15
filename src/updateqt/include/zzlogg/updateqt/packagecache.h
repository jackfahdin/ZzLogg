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
}
