#include "zzlogg/updateqt/packagecache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStorageInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace zzlogg::updateqt {
namespace {
constexpr quint64 maxPackageSize=512ULL*1024*1024;
constexpr quint64 safetyMargin=16ULL*1024*1024;

bool isLinkOrReparsePoint(const QString& path)
{
#ifdef Q_OS_WIN
    const auto native=QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes=GetFileAttributesW(native.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES
        && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return QFileInfo(path).isSymbolicLink();
#endif
}

bool hasSafeExistingPath(const QString& path)
{
    QString current=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!current.isEmpty()) {
        if (isLinkOrReparsePoint(current)) return false;
        const QString parent=QDir::cleanPath(QFileInfo(current).dir().absolutePath());
        if (parent==current) break;
        current=parent;
    }
    return true;
}

bool validHash(const std::string& hash)
{
    if (hash.size()!=64) return false;
    for (const char ch:hash)
        if (!((ch>='0' && ch<='9') || (ch>='a' && ch<='f'))) return false;
    return true;
}

class QtPackageCacheStorage final : public PackageCacheStorage {
public:
    std::optional<quint64> availableBytes(const QString& path) const override
    {
        const QStorageInfo storage(path);
        if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable()<0)
            return std::nullopt;
        return quint64(storage.bytesAvailable());
    }

    std::unique_ptr<QSaveFile> createSaveFile(const QString& path) const override
    {
        return std::make_unique<QSaveFile>(path);
    }
};
}

class PackageCache::Impl {
public:
    Impl(QString root, update::Artifact value,
         std::shared_ptr<const PackageCacheStorage> storageValue = {})
        : cacheRoot(std::move(root)), artifact(std::move(value)), storage(std::move(storageValue)) {}
    QString cacheRoot;
    update::Artifact artifact;
    std::shared_ptr<const PackageCacheStorage> storage;
    QString verifiedPath;
    QString destination;
    std::unique_ptr<QLockFile> lock;
    std::unique_ptr<QSaveFile> file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    quint64 received = 0;
    bool active = false;

    void cleanUp()
    {
        if (file) file->cancelWriting();
        file.reset();
        if (lock) lock->unlock();
        lock.reset();
        active=false;
    }

    CacheError fail(CacheError error)
    {
        verifiedPath.clear();
        cleanUp();
        return error;
    }
};

PackageCache::PackageCache(QString cacheRoot, update::Artifact artifact)
    : PackageCache(std::move(cacheRoot),std::move(artifact),
                   std::make_shared<QtPackageCacheStorage>()) {}
PackageCache::PackageCache(QString cacheRoot, update::Artifact artifact,
                           std::shared_ptr<const PackageCacheStorage> storage)
    : impl_(std::make_unique<Impl>(std::move(cacheRoot),std::move(artifact),std::move(storage))) {}
PackageCache::~PackageCache() { impl_->cleanUp(); }

CacheError PackageCache::begin()
{
    if (impl_->active) return CacheError::Busy;
    impl_->verifiedPath.clear();
    if (impl_->artifact.size<1 || impl_->artifact.size>maxPackageSize
        || !validHash(impl_->artifact.sha256)) return CacheError::InvalidArtifact;
    if (impl_->cacheRoot.isEmpty() || !QDir::isAbsolutePath(impl_->cacheRoot))
        return CacheError::InvalidPath;
    impl_->cacheRoot=QDir::cleanPath(impl_->cacheRoot);
    if (!hasSafeExistingPath(impl_->cacheRoot)) return CacheError::InvalidPath;
    const QFileInfo existingRoot(impl_->cacheRoot);
    if (existingRoot.exists() && !existingRoot.isDir()) return CacheError::InvalidPath;
    if (!QDir().mkpath(impl_->cacheRoot)) return CacheError::InvalidPath;
    const QFileInfo rootInfo(impl_->cacheRoot);
    if (!rootInfo.isDir() || !hasSafeExistingPath(impl_->cacheRoot))
        return CacheError::InvalidPath;
    impl_->destination=QDir(impl_->cacheRoot).filePath(
        QString::fromLatin1(impl_->artifact.sha256)+QStringLiteral(".package"));
    const QString lockPath=impl_->destination+QStringLiteral(".lock");
    if (!hasSafeExistingPath(impl_->destination) || !hasSafeExistingPath(lockPath))
        return CacheError::InvalidPath;
    if (QFileInfo(impl_->destination).exists() && QFileInfo(impl_->destination).isDir())
        return CacheError::InvalidPath;
    if (!impl_->storage) return CacheError::InsufficientSpace;
    const auto available=impl_->storage->availableBytes(impl_->cacheRoot);
    const quint64 required=impl_->artifact.size+safetyMargin;
    if (!available || *available<required) return CacheError::InsufficientSpace;
    impl_->lock=std::make_unique<QLockFile>(lockPath);
    impl_->lock->setStaleLockTime(0);
    if (!impl_->lock->tryLock(0)) {
        const auto lockError=impl_->lock->error();
        impl_->lock.reset();
        return lockError==QLockFile::LockFailedError ? CacheError::Busy : CacheError::WriteFailed;
    }
    impl_->file=impl_->storage->createSaveFile(impl_->destination);
    if (!impl_->file) return impl_->fail(CacheError::WriteFailed);
    impl_->file->setDirectWriteFallback(false);
    if (!impl_->file->open(QIODevice::WriteOnly)) return impl_->fail(CacheError::WriteFailed);
    impl_->hash.reset();
    impl_->received=0;
    impl_->active=true;
    return CacheError::None;
}
CacheError PackageCache::append(QByteArrayView bytes)
{
    if (!impl_->active || !impl_->file) return impl_->fail(CacheError::WriteFailed);
    if (quint64(bytes.size())>impl_->artifact.size-impl_->received)
        return impl_->fail(CacheError::SizeMismatch);
    if (impl_->file->write(bytes.data(),bytes.size())!=bytes.size())
        return impl_->fail(CacheError::WriteFailed);
    impl_->hash.addData(bytes);
    impl_->received+=quint64(bytes.size());
    return CacheError::None;
}
CacheError PackageCache::finish()
{
    if (!impl_->active || !impl_->file) return impl_->fail(CacheError::WriteFailed);
    if (impl_->received!=impl_->artifact.size) return impl_->fail(CacheError::SizeMismatch);
    if (impl_->hash.result().toHex().toStdString()!=impl_->artifact.sha256)
        return impl_->fail(CacheError::HashMismatch);
    if (!impl_->file->commit()) return impl_->fail(CacheError::WriteFailed);
    impl_->file.reset();
    impl_->lock->unlock();
    impl_->lock.reset();
    impl_->active=false;
    impl_->verifiedPath=impl_->destination;
    return CacheError::None;
}
CacheError PackageCache::cancel()
{
    impl_->verifiedPath.clear();
    impl_->cleanUp();
    return CacheError::Cancelled;
}
QString PackageCache::verifiedPath() const { return impl_->verifiedPath; }
}
