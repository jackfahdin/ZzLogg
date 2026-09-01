#include "storagecontext.h"

#include <QTemporaryDir>
#include <QDir>
#include <QtTest/QtTest>

#include <atomic>
#include <thread>
#include <vector>

class StorageContextTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void exposesStableLayout();
    void normalizesNativePaths();
    void supportsAllStorageModes();
    void createsFixedDirectories();
    void concurrentInstallSucceedsExactlyOnce();
    void installsOnlyOnce();
};

void StorageContextTest::exposesStableLayout()
{
    const StorageLocation location{ StorageMode::CustomDirectory,
                                    QStringLiteral( "C:/data/ZzLogg" ),
                                    QStringLiteral( "C:/bootstrap/storage.ini" ), false };
    const StorageContext context{ location };
    QCOMPARE( context.configFilePath(), QStringLiteral( "C:/data/ZzLogg/config/ZzLogg.ini" ) );
    QCOMPARE( context.sessionFilePath(),
              QStringLiteral( "C:/data/ZzLogg/session/ZzLogg_session.ini" ) );
    QCOMPARE( context.logsDirectory(), QStringLiteral( "C:/data/ZzLogg/logs" ) );
    QCOMPARE( context.crashesDirectory(), QStringLiteral( "C:/data/ZzLogg/crashes" ) );
    QCOMPARE( context.manifestFilePath(),
              QStringLiteral( "C:/data/ZzLogg/storage-manifest.ini" ) );
}

void StorageContextTest::normalizesNativePaths()
{
    const StorageContext context{ { StorageMode::CustomDirectory,
                                    QStringLiteral( "C:\\data\\.\\ZzLogg\\..\\ZzLogg\\" ),
                                    QStringLiteral( "C:\\bootstrap\\folder\\..\\storage.ini" ), false } };
    QCOMPARE( context.dataRoot(), QStringLiteral( "C:/data/ZzLogg" ) );
    QCOMPARE( context.location().locatorPath, QStringLiteral( "C:/bootstrap/storage.ini" ) );
    QCOMPARE( context.configFilePath(), QStringLiteral( "C:/data/ZzLogg/config/ZzLogg.ini" ) );
}

void StorageContextTest::supportsAllStorageModes()
{
    const QList<StorageMode> modes{ StorageMode::UserDirectory,
                                    StorageMode::ProgramDirectory,
                                    StorageMode::CustomDirectory };
    for ( const auto mode : modes ) {
        const StorageLocation location{ mode, QStringLiteral( "C:/data/ZzLogg" ), {}, false };
        const StorageContext context{ location };
        QCOMPARE( context.location().mode, mode );
        QCOMPARE( context.dataRoot(), QStringLiteral( "C:/data/ZzLogg" ) );
        QCOMPARE( context.location().locatorPath, QString() );
        QCOMPARE( context.configDirectory(), QStringLiteral( "C:/data/ZzLogg/config" ) );
    }
}

void StorageContextTest::createsFixedDirectories()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    const QString dataRoot = root.filePath( "new-storage-root" );
    QVERIFY( !QDir( dataRoot ).exists() );
    const StorageContext context{ { StorageMode::UserDirectory, dataRoot, {}, false } };
    QString error;
    QVERIFY2( context.ensureDirectories( &error ), qPrintable( error ) );
    QVERIFY( QDir( context.dataRoot() ).exists() );
    QVERIFY( QDir( context.configDirectory() ).exists() );
    QVERIFY( QDir( context.sessionDirectory() ).exists() );
    QVERIFY( QDir( context.logsDirectory() ).exists() );
    QVERIFY( QDir( context.crashesDirectory() ).exists() );
}

void StorageContextTest::concurrentInstallSucceedsExactlyOnce()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    constexpr int threadCount = 32;
    std::atomic<int> ready{ 0 };
    std::atomic<bool> start{ false };
    std::atomic<int> successes{ 0 };
    std::vector<StorageLocation> locations;
    locations.reserve( threadCount );
    for ( int i = 0; i < threadCount; ++i ) {
        locations.push_back( { StorageMode::CustomDirectory,
                               root.filePath( QStringLiteral( "root-%1" ).arg( i ) ), {}, false } );
    }
    std::vector<std::thread> workers;
    workers.reserve( threadCount );
    for ( int i = 0; i < threadCount; ++i ) {
        workers.emplace_back( [&, i] {
            ++ready;
            while ( !start.load( std::memory_order_acquire ) ) {
                std::this_thread::yield();
            }
            QString error;
            if ( StorageContext::install( locations.at( i ), &error ) ) {
                ++successes;
            }
        } );
    }
    while ( ready.load( std::memory_order_acquire ) != threadCount ) {
        std::this_thread::yield();
    }
    start.store( true, std::memory_order_release );
    for ( auto& worker : workers ) {
        worker.join();
    }
    QCOMPARE( successes.load(), 1 );
    QVERIFY( StorageContext::isInstalled() );
}

void StorageContextTest::installsOnlyOnce()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    QVERIFY( StorageContext::isInstalled() );
    QString error;
    QVERIFY( !StorageContext::install(
        { StorageMode::CustomDirectory, root.filePath( "other" ),
          root.filePath( "other.ini" ), false }, &error ) );
    QVERIFY( error.contains( "already installed" ) );
}

QTEST_MAIN( StorageContextTest )
#include "storagecontexttest.moc"
