#include "storagecontext.h"

#include <QTemporaryDir>
#include <QDir>
#include <QtTest/QtTest>

class StorageContextTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void exposesStableLayout();
    void supportsAllStorageModes();
    void createsFixedDirectories();
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
        QCOMPARE( context.configDirectory(), QStringLiteral( "C:/data/ZzLogg/config" ) );
    }
}

void StorageContextTest::createsFixedDirectories()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    const StorageContext context{ { StorageMode::UserDirectory, root.path(), {}, false } };
    QString error;
    QVERIFY2( context.ensureDirectories( &error ), qPrintable( error ) );
    QVERIFY( QDir( context.configDirectory() ).exists() );
    QVERIFY( QDir( context.sessionDirectory() ).exists() );
    QVERIFY( QDir( context.logsDirectory() ).exists() );
    QVERIFY( QDir( context.crashesDirectory() ).exists() );
}

void StorageContextTest::installsOnlyOnce()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    QString error;
    QVERIFY( StorageContext::install(
        { StorageMode::UserDirectory, root.path(), root.filePath( "storage.ini" ), false },
        &error ) );
    QVERIFY( StorageContext::isInstalled() );
    QVERIFY( !StorageContext::install(
        { StorageMode::CustomDirectory, root.filePath( "other" ),
          root.filePath( "other.ini" ), false }, &error ) );
    QVERIFY( error.contains( "already installed" ) );
}

QTEST_MAIN( StorageContextTest )
#include "storagecontexttest.moc"
