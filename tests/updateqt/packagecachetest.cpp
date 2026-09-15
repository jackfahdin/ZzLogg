#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <limits>
#include "zzlogg/updateqt/packagecache.h"

using namespace zzlogg::updateqt;

namespace {
constexpr quint64 maxPackageSize = 512ULL*1024*1024;
constexpr quint64 safetyMargin = 16ULL*1024*1024;

zzlogg::update::Artifact artifactFor(const QByteArray& bytes)
{
    zzlogg::update::Artifact artifact;
    artifact.size=quint64(bytes.size());
    artifact.sha256=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
    return artifact;
}

QString packagePath(const QString& root,const zzlogg::update::Artifact& artifact)
{
    return QDir(root).filePath(QString::fromLatin1(artifact.sha256)+QStringLiteral(".package"));
}

#ifdef Q_OS_WIN
bool createJunction(const QString& link,const QString& target)
{
    QProcess process;
    process.start(QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"),QStringLiteral("mklink"),QStringLiteral("/J"),
         QDir::toNativeSeparators(link),QDir::toNativeSeparators(target)});
    return process.waitForFinished() && process.exitStatus()==QProcess::NormalExit
        && process.exitCode()==0;
}
#endif

class FailingSaveFile final : public QSaveFile {
public:
    explicit FailingSaveFile(const QString& path) : QSaveFile(path) {}
protected:
    qint64 writeData(const char*,qint64) override { return -1; }
};

class PartialSaveFile final : public QSaveFile {
public:
    explicit PartialSaveFile(const QString& path) : QSaveFile(path) {}
protected:
    qint64 writeData(const char*,qint64 length) override { return length>1 ? length-1 : 0; }
};

class TestStorage final : public PackageCacheStorage {
public:
    std::optional<quint64> available=std::numeric_limits<quint64>::max();
    bool failWrites=false;
    bool partialWrites=false;

    std::optional<quint64> availableBytes(const QString&) const override { return available; }
    std::unique_ptr<QSaveFile> createSaveFile(const QString& path) const override
    {
        if (failWrites) return std::make_unique<FailingSaveFile>(path);
        if (partialWrites) return std::make_unique<PartialSaveFile>(path);
        return std::make_unique<QSaveFile>(path);
    }
};
}

class PackageCacheTest : public QObject {
    Q_OBJECT
private slots:
    void invalidArtifacts_data()
    {
        QTest::addColumn<qulonglong>("size");
        QTest::addColumn<QByteArray>("hash");
        const QByteArray valid(64,'a');
        QTest::newRow("zero-length") << qulonglong(0) << valid;
        QTest::newRow("over-limit") << qulonglong(maxPackageSize+1) << valid;
        QTest::newRow("short-hash") << qulonglong(1) << QByteArray(63,'a');
        QTest::newRow("long-hash") << qulonglong(1) << QByteArray(65,'a');
        QTest::newRow("uppercase-hash") << qulonglong(1) << QByteArray(64,'A');
        QByteArray nonHex(64,'a'); nonHex[12]='g';
        QTest::newRow("non-hex-hash") << qulonglong(1) << nonHex;
    }

    void invalidArtifacts()
    {
        QFETCH(qulonglong,size);
        QFETCH(QByteArray,hash);
        QTemporaryDir root;
        zzlogg::update::Artifact artifact;
        artifact.size=size;
        artifact.sha256=hash.toStdString();
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::InvalidArtifact);
        QVERIFY(cache.verifiedPath().isEmpty());
    }

    void acceptsUpperSizeBoundary()
    {
        QTemporaryDir root;
        auto artifact=artifactFor("x");
        artifact.size=maxPackageSize;
        auto storage=std::make_shared<TestStorage>();
        PackageCache cache(root.path(),artifact,storage);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.cancel(),CacheError::Cancelled);
    }

    void invalidRoots_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("empty") << 0;
        QTest::newRow("relative") << 1;
        QTest::newRow("file") << 2;
    }

    void invalidRoots()
    {
        QFETCH(int,kind);
        QTemporaryDir temporary;
        QString root;
        if (kind==1) root=QStringLiteral("relative-cache");
        if (kind==2) {
            root=temporary.filePath(QStringLiteral("not-a-directory"));
            QFile file(root); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("keep"); file.close();
        }
        PackageCache cache(root,artifactFor("x"));
        QCOMPARE(cache.begin(),CacheError::InvalidPath);
    }

    void rejectsReparsePoint()
    {
        QTemporaryDir temporary;
        const QString target=temporary.filePath(QStringLiteral("target"));
        const QString link=temporary.filePath(QStringLiteral("link"));
        QVERIFY(QDir().mkpath(target));
#ifdef Q_OS_WIN
        QVERIFY(createJunction(link,target));
#else
        QVERIFY(QFile::link(target,link));
#endif
        PackageCache cache(QDir(link).filePath(QStringLiteral("cache")),artifactFor("x"));
        QCOMPARE(cache.begin(),CacheError::InvalidPath);
        QVERIFY(!QFileInfo::exists(QDir(target).filePath(QStringLiteral("cache"))));
    }

    void rejectsReparsePointAsCacheRoot()
    {
        QTemporaryDir temporary;
        const QString target=temporary.filePath(QStringLiteral("target"));
        const QString link=temporary.filePath(QStringLiteral("link"));
        QVERIFY(QDir().mkpath(target));
#ifdef Q_OS_WIN
        QVERIFY(createJunction(link,target));
#else
        QVERIFY(QFile::link(target,link));
#endif
        PackageCache cache(link,artifactFor("x"));
        QCOMPARE(cache.begin(),CacheError::InvalidPath);
        QVERIFY(QFileInfo(link).exists());
        QVERIFY(QDir(target).isEmpty());
    }

    void rejectsTargetAndLockReparsePoints_data()
    {
        QTest::addColumn<bool>("lockPath");
        QTest::newRow("package-target") << false;
        QTest::newRow("lock-target") << true;
    }

    void rejectsTargetAndLockReparsePoints()
    {
        QFETCH(bool,lockPath);
        QTemporaryDir root;
        const auto artifact=artifactFor("x");
        QString path=packagePath(root.path(),artifact);
        if (lockPath) path+=QStringLiteral(".lock");
        const QString target=root.filePath(QStringLiteral("junction-target"));
        QVERIFY(QDir().mkpath(target));
#ifdef Q_OS_WIN
        QVERIFY(createJunction(path,target));
#else
        QVERIFY(QFile::link(target,path));
#endif
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::InvalidPath);
        QVERIFY(QFileInfo(path).exists());
    }

    void rejectsUnknownAndInsufficientSpace_data()
    {
        QTest::addColumn<bool>("known");
        QTest::addColumn<qulonglong>("available");
        QTest::newRow("unknown") << false << qulonglong(0);
        QTest::newRow("one-byte-short") << true << qulonglong(safetyMargin);
    }

    void rejectsUnknownAndInsufficientSpace()
    {
        QFETCH(bool,known);
        QFETCH(qulonglong,available);
        QTemporaryDir root;
        auto storage=std::make_shared<TestStorage>();
        storage->available=known ? std::optional<quint64>(available) : std::nullopt;
        PackageCache cache(root.path(),artifactFor("x"),storage);
        QCOMPARE(cache.begin(),CacheError::InsufficientSpace);
    }

    void locksUntilCancel()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("x");
        PackageCache first(root.path(),artifact),second(root.path(),artifact);
        QCOMPARE(first.begin(),CacheError::None);
        QCOMPARE(first.begin(),CacheError::Busy);
        QCOMPARE(second.begin(),CacheError::Busy);
        QCOMPARE(first.cancel(),CacheError::Cancelled);
        QCOMPARE(second.begin(),CacheError::None);
        QCOMPARE(second.cancel(),CacheError::Cancelled);
    }

    void rejectsShortPackage()
    {
        QTemporaryDir root;
        const QByteArray bytes("package");
        const auto artifact=artifactFor(bytes);
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(bytes.chopped(1)),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::SizeMismatch);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
    }

    void rejectsLongPackageImmediately()
    {
        QTemporaryDir root;
        auto artifact=artifactFor("x");
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("xy",2)),CacheError::SizeMismatch);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
        PackageCache retry(root.path(),artifact);
        QCOMPARE(retry.begin(),CacheError::None);
        QCOMPARE(retry.cancel(),CacheError::Cancelled);
    }

    void rejectsWrongHash()
    {
        QTemporaryDir root;
        auto artifact=artifactFor("expected");
        artifact.size=5;
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("wrong",5)),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::HashMismatch);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
    }

    void terminatesOnWriteFailure()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("x");
        auto storage=std::make_shared<TestStorage>();
        storage->failWrites=true;
        PackageCache cache(root.path(),artifact,storage);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("x",1)),CacheError::WriteFailed);
        QVERIFY(cache.verifiedPath().isEmpty());
        PackageCache retry(root.path(),artifact);
        QCOMPARE(retry.begin(),CacheError::None);
        QCOMPARE(retry.cancel(),CacheError::Cancelled);
    }

    void terminatesOnPositivePartialWrite()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("xy");
        auto storage=std::make_shared<TestStorage>();
        storage->partialWrites=true;
        PackageCache cache(root.path(),artifact,storage);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("xy",2)),CacheError::WriteFailed);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
        PackageCache retry(root.path(),artifact);
        QCOMPARE(retry.begin(),CacheError::None);
        QCOMPARE(retry.cancel(),CacheError::Cancelled);
    }

    void rejectsCommitFailure()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("x");
        const QString destination=packagePath(root.path(),artifact);
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("x",1)),CacheError::None);
        QVERIFY(QDir().mkpath(destination));
        QCOMPARE(cache.finish(),CacheError::WriteFailed);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(QFileInfo(destination).isDir());
    }

    void cancelIsIdempotent()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("package");
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("pack",4)),CacheError::None);
        QCOMPARE(cache.cancel(),CacheError::Cancelled);
        QCOMPARE(cache.cancel(),CacheError::Cancelled);
        QVERIFY(cache.verifiedPath().isEmpty());
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
    }

    void destructorCancelsIncompleteWrite()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("package");
        {
            PackageCache cache(root.path(),artifact);
            QCOMPARE(cache.begin(),CacheError::None);
            QCOMPARE(cache.append(QByteArrayView("pack",4)),CacheError::None);
        }
        QVERIFY(!QFileInfo::exists(packagePath(root.path(),artifact)));
        PackageCache retry(root.path(),artifact);
        QCOMPARE(retry.begin(),CacheError::None);
        QCOMPARE(retry.cancel(),CacheError::Cancelled);
    }

    void failurePreservesOldPackage()
    {
        QTemporaryDir root;
        const QByteArray replacement("new");
        auto artifact=artifactFor("expected");
        artifact.size=replacement.size();
        const QString destination=packagePath(root.path(),artifact);
        QFile old(destination); QVERIFY(old.open(QIODevice::WriteOnly));
        QCOMPARE(old.write("old"),qint64(3)); old.close();
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(replacement),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::HashMismatch);
        QFile preserved(destination); QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(),QByteArray("old"));
    }

    void streamsAndCommitsVerifiedPackage()
    {
        QTemporaryDir root;
        const QByteArray bytes("verified package");
        zzlogg::update::Artifact artifact;
        artifact.size=bytes.size();
        artifact.sha256=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
        artifact.url="https://updates.example.invalid/not-the-cache-name.exe";
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QVERIFY(cache.verifiedPath().isEmpty());
        QCOMPARE(cache.append(bytes.first(4)),CacheError::None);
        QCOMPARE(cache.append(bytes.sliced(4)),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::None);
        QFile saved(cache.verifiedPath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(),bytes);
        QCOMPARE(QFileInfo(cache.verifiedPath()).fileName(),
                 QString::fromLatin1(artifact.sha256)+QStringLiteral(".package"));
    }

    void successReplacesOldPackage()
    {
        QTemporaryDir root;
        const QByteArray bytes("replacement");
        const auto artifact=artifactFor(bytes);
        const QString destination=packagePath(root.path(),artifact);
        QFile old(destination); QVERIFY(old.open(QIODevice::WriteOnly));
        QCOMPARE(old.write("old"),qint64(3)); old.close();
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(bytes),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::None);
        QFile replaced(destination); QVERIFY(replaced.open(QIODevice::ReadOnly));
        QCOMPARE(replaced.readAll(),bytes);
    }

    void errorsClearPreviouslyVerifiedPath()
    {
        QTemporaryDir root;
        const auto artifact=artifactFor("x");
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("x",1)),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::None);
        QVERIFY(!cache.verifiedPath().isEmpty());
        QCOMPARE(cache.finish(),CacheError::WriteFailed);
        QVERIFY(cache.verifiedPath().isEmpty());
    }

    void preservesUnknownNeighbors()
    {
        QTemporaryDir root;
        const QString unknown=root.filePath(QStringLiteral("unknown.bin"));
        const QString directory=root.filePath(QStringLiteral("adjacent"));
        QFile file(unknown); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep"),qint64(4)); file.close();
        QVERIFY(QDir().mkpath(directory));
        const auto artifact=artifactFor("x");
        PackageCache cache(root.path(),artifact);
        QCOMPARE(cache.begin(),CacheError::None);
        QCOMPARE(cache.append(QByteArrayView("x",1)),CacheError::None);
        QCOMPARE(cache.finish(),CacheError::None);
        QFile preserved(unknown); QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(),QByteArray("keep"));
        QVERIFY(QFileInfo(directory).isDir());
    }
};

QTEST_GUILESS_MAIN(PackageCacheTest)
#include "packagecachetest.moc"
