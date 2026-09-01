#include "storagemigrator.h"

#include "storagevalidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

bool writeBytes( const QString& path, const QByteArray& contents )
{
    if ( !QDir{}.mkpath( QFileInfo{ path }.absolutePath() ) ) {
        return false;
    }
    QSaveFile file{ path };
    return file.open( QIODevice::WriteOnly ) && file.write( contents ) == contents.size()
           && file.commit();
}

QByteArray readBytes( const QString& path )
{
    QFile file{ path };
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
}

bool atomicCopy( const QString& source, const QString& target, QString* error )
{
    QFile input{ source };
    QSaveFile output{ target };
    if ( !input.open( QIODevice::ReadOnly ) || !output.open( QIODevice::WriteOnly ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "fixture copy failed from %1 to %2" ).arg( source, target );
        }
        return false;
    }
    const QByteArray contents = input.readAll();
    if ( input.error() != QFileDevice::NoError || output.write( contents ) != contents.size()
         || !output.commit() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "fixture copy failed from %1 to %2" ).arg( source, target );
        }
        return false;
    }
    return true;
}

bool writeLocator( const QString& path, const QString& mode, const QString& dataRoot )
{
    const QByteArray contents
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=%1\ndataRoot=%2\nverified=true\n" )
              .arg( mode, dataRoot )
              .toUtf8();
    return writeBytes( path, contents );
}

struct MigrationFixture {
    explicit MigrationFixture( QTemporaryDir& temporaryDirectory )
        : applicationDirectory( temporaryDirectory.filePath( QStringLiteral( "app" ) ) )
        , appConfigDirectory( temporaryDirectory.filePath( QStringLiteral( "app-config" ) ) )
        , legacyDirectory( temporaryDirectory.filePath( QStringLiteral( "legacy" ) ) )
        , targetRoot( temporaryDirectory.filePath( QStringLiteral( "target" ) ) )
        , store( applicationDirectory, appConfigDirectory )
        , source{ StorageMode::UserDirectory,
                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                  store.userLocatorPath(), false }
        , target{ StorageMode::ProgramDirectory, targetRoot, store.programLocatorPath(), false }
        , request{ QStringLiteral( "migration-transaction" ),
                   source,
                   target,
                   QDir{ legacyDirectory }.filePath( QStringLiteral( "ZzLogg.ini" ) ),
                   QDir{ legacyDirectory }.filePath( QStringLiteral( "ZzLogg_session.ini" ) ),
                   QDir{ legacyDirectory }.filePath( QStringLiteral( "klogg_dump" ) ) }
    {
    }

    bool initializeLocator()
    {
        QString error;
        return store.writeActive( source, &error );
    }

    QString applicationDirectory;
    QString appConfigDirectory;
    QString legacyDirectory;
    QString targetRoot;
    StorageLocatorStore store;
    StorageLocation source;
    StorageLocation target;
    StorageMigrationRequest request;
};

void verifySourceIsActiveWithoutPending( const MigrationFixture& fixture )
{
    const auto resolution = fixture.store.resolve();
    QVERIFY2( resolution.state.has_value(), qPrintable( resolution.error ) );
    QCOMPARE( resolution.state->active.dataRoot, QDir::cleanPath( fixture.source.dataRoot ) );
    QVERIFY( resolution.state->verified );
    QVERIFY( !resolution.state->pending.has_value() );
}

} // namespace

class StorageMigratorTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void migratesIniCrashTreeManifestAndLocatorWithoutChangingSource();
    void missingLegacyIniCreatesReadableEmptyTarget_data();
    void missingLegacyIniCreatesReadableEmptyTarget();
    void emptyIniCreationFailureNamesSourceAndTarget();
    void sessionCopyFailureRollsBackAndPreservesPreexistingTargetContent();
    void recursiveCrashCopyUsesInjectedOperation();
    void lockConflictFailsBeforeWritingPendingState();
    void normalizedPendingDrivesLockTargetAndRecovery();
    void preexistingDestinationIsNeverOverwritten_data();
    void preexistingDestinationIsNeverOverwritten();
    void rejectsCrashTreeSymlinkWhenPlatformSupportsIt();
    void rollbackFailureReportsBothErrors();
    void recoverUsesMigrationLock();
    void recoverReadsSourcePendingDespiteProgramLocatorPriority();
    void recoverCompletePendingCommitsIdempotently();
    void recoverIncompletePendingRollsBackIdempotently();
};

void StorageMigratorTest::migratesIniCrashTreeManifestAndLocatorWithoutChangingSource()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const QByteArray config{ "[General]\nconfigValue=alpha\n" };
    const QByteArray session{ "[General]\nsessionValue=beta\n" };
    const QByteArray crash{ "crash-payload" };
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, config ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, session ) );
    const QString legacyCrash = QDir{ fixture.request.legacyCrashDirectory }.filePath(
        QStringLiteral( "nested/2026/dump.dmp" ) );
    QVERIFY( writeBytes( legacyCrash, crash ) );
    QStringList copyTargets;
    const StorageCopyOperation copy
        = [ &copyTargets ]( const QString& source, const QString& target, QString* error ) {
              copyTargets.append( QDir::cleanPath( target ) );
              return atomicCopy( source, target, error );
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY2( result.success, qPrintable( result.error ) );
    QVERIFY( !result.rolledBack );
    const StorageContext targetContext{ fixture.target };
    QCOMPARE( readBytes( targetContext.configFilePath() ), config );
    QCOMPARE( readBytes( targetContext.sessionFilePath() ), session );
    QCOMPARE( readBytes( QDir{ targetContext.crashesDirectory() }.filePath(
                  QStringLiteral( "nested/2026/dump.dmp" ) ) ),
              crash );
    QVERIFY( StorageValidator::hasCompatibleManifest( fixture.targetRoot ) );
    QCOMPARE( copyTargets,
              QStringList( { QDir::cleanPath( targetContext.configFilePath() ),
                             QDir::cleanPath( targetContext.sessionFilePath() ),
                             QDir::cleanPath( QDir{ targetContext.crashesDirectory() }.filePath(
                                 QStringLiteral( "nested/2026/dump.dmp" ) ) ) } ) );
    const auto resolution = fixture.store.resolve();
    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY2( resolution.state.has_value(), qPrintable( resolution.error ) );
    QCOMPARE( resolution.state->active.dataRoot, QDir::cleanPath( fixture.targetRoot ) );
    QVERIFY( !resolution.state->verified );
    QVERIFY( !resolution.state->pending.has_value() );
    QVERIFY( QFile::exists( fixture.request.legacyConfigFile ) );
    QVERIFY( QFile::exists( fixture.request.legacySessionFile ) );
    QVERIFY( QFile::exists( legacyCrash ) );
}

void StorageMigratorTest::missingLegacyIniCreatesReadableEmptyTarget_data()
{
    QTest::addColumn<bool>( "configExists" );
    QTest::newRow( "config-only" ) << true;
    QTest::newRow( "session-only" ) << false;
}

void StorageMigratorTest::missingLegacyIniCreatesReadableEmptyTarget()
{
    QFETCH( bool, configExists );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const QByteArray existing{ "[General]\nkept=value\n" };
    const QString existingSource
        = configExists ? fixture.request.legacyConfigFile : fixture.request.legacySessionFile;
    const QString missingSource
        = configExists ? fixture.request.legacySessionFile : fixture.request.legacyConfigFile;
    QVERIFY( writeBytes( existingSource, existing ) );
    QStringList copiedSources;
    const StorageCopyOperation copy
        = [ &copiedSources ]( const QString& source, const QString& target, QString* error ) {
              copiedSources.append( source );
              return atomicCopy( source, target, error );
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY2( result.success, qPrintable( result.error ) );
    const StorageContext context{ fixture.target };
    const QString existingTarget
        = configExists ? context.configFilePath() : context.sessionFilePath();
    const QString emptyTarget = configExists ? context.sessionFilePath() : context.configFilePath();
    QCOMPARE( readBytes( existingTarget ), existing );
    QVERIFY( QFileInfo{ emptyTarget }.isFile() );
    QSettings emptySettings{ emptyTarget, QSettings::IniFormat };
    QCOMPARE( emptySettings.status(), QSettings::NoError );
    QCOMPARE( copiedSources, QStringList{ existingSource } );
    QVERIFY( !QFile::exists( missingSource ) );
    QCOMPARE( readBytes( existingSource ), existing );
}

void StorageMigratorTest::emptyIniCreationFailureNamesSourceAndTarget()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacySessionFile,
                         QByteArray{ "[General]\nsession=kept\n" } ) );
    const QString targetConfig = StorageContext{ fixture.target }.configFilePath();
    QVERIFY( QDir{}.mkpath( targetConfig ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( fixture.request.legacyConfigFile ) );
    QVERIFY( result.error.contains( targetConfig ) );
    QVERIFY( QFileInfo{ targetConfig }.isDir() );
    verifySourceIsActiveWithoutPending( fixture );
}

void StorageMigratorTest::sessionCopyFailureRollsBackAndPreservesPreexistingTargetContent()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY(
        writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\nconfig=kept\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile,
                         QByteArray{ "[General]\nsession=kept\n" } ) );
    const QString preexisting = QDir{ fixture.targetRoot }.filePath( QStringLiteral( "keep.txt" ) );
    QVERIFY( writeBytes( preexisting, QByteArray{ "do-not-remove" } ) );
    const QString targetSession = StorageContext{ fixture.target }.sessionFilePath();
    const StorageCopyOperation copy
        = [ targetSession ]( const QString& source, const QString& target, QString* error ) {
              if ( QDir::cleanPath( target ) == QDir::cleanPath( targetSession ) ) {
                  if ( !atomicCopy( source, target, error ) ) {
                      return false;
                  }
                  if ( error != nullptr ) {
                      *error = QStringLiteral( "injected failure after target creation" );
                  }
                  return false;
              }
              return atomicCopy( source, target, error );
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "injected failure after target creation" ) ) );
    QVERIFY( result.error.contains( fixture.request.legacySessionFile ) );
    QVERIFY( result.error.contains( targetSession ) );
    verifySourceIsActiveWithoutPending( fixture );
    QVERIFY( !StorageValidator::hasCompatibleManifest( fixture.targetRoot ) );
    QVERIFY( !QFile::exists( StorageContext{ fixture.target }.configFilePath() ) );
    QVERIFY( !QFile::exists( targetSession ) );
    QCOMPARE( readBytes( preexisting ), QByteArray{ "do-not-remove" } );
    QVERIFY( QFile::exists( fixture.request.legacyConfigFile ) );
    QVERIFY( QFile::exists( fixture.request.legacySessionFile ) );
}

void StorageMigratorTest::recursiveCrashCopyUsesInjectedOperation()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    const QString crashSource = QDir{ fixture.request.legacyCrashDirectory }.filePath(
        QStringLiteral( "nested/fail.dmp" ) );
    QVERIFY( writeBytes( crashSource, QByteArray{ "dump" } ) );
    const StorageCopyOperation copy
        = [ crashSource ]( const QString& source, const QString& target, QString* error ) {
              if ( QDir::cleanPath( source ) == QDir::cleanPath( crashSource ) ) {
                  if ( error != nullptr ) {
                      *error = QStringLiteral( "injected crash failure" );
                  }
                  return false;
              }
              return atomicCopy( source, target, error );
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( crashSource ) );
    QVERIFY( result.error.contains( QStringLiteral( "fail.dmp" ) ) );
    verifySourceIsActiveWithoutPending( fixture );
}

void StorageMigratorTest::lockConflictFailsBeforeWritingPendingState()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    const QString lockPath = fixture.source.locatorPath + QStringLiteral( ".migration.lock" );
    QLockFile heldLock{ lockPath };
    heldLock.setStaleLockTime( 0 );
    QVERIFY( heldLock.tryLock( 0 ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "lock" ), Qt::CaseInsensitive ) );
    QVERIFY( result.error.contains( QDir::cleanPath( lockPath ) ) );
    verifySourceIsActiveWithoutPending( fixture );
    QVERIFY( !QFile::exists( fixture.targetRoot ) );
}

void StorageMigratorTest::normalizedPendingDrivesLockTargetAndRecovery()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    StorageMigrationRequest rawRequest = fixture.request;
    rawRequest.source.locatorPath.clear();
    rawRequest.target.locatorPath.clear();
    rawRequest.target.dataRoot = QStringLiteral( "data" );
    const QString canonicalRoot
        = QDir{ fixture.applicationDirectory }.filePath( QStringLiteral( "data" ) );
    const QString canonicalConfig = StorageContext{
        { StorageMode::ProgramDirectory, canonicalRoot, fixture.store.programLocatorPath(), false }
    }.configFilePath();

    const StorageMigrationResult executed = StorageMigrator{ fixture.store }.execute( rawRequest );
    const StorageMigrationResult recovered
        = StorageMigrator{ fixture.store }.recoverPending( rawRequest );

    QVERIFY2( executed.success, qPrintable( executed.error ) );
    QVERIFY( QFileInfo{ canonicalConfig }.isFile() );
    QVERIFY2( recovered.success, qPrintable( recovered.error ) );
}

void StorageMigratorTest::preexistingDestinationIsNeverOverwritten_data()
{
    QTest::addColumn<QString>( "destinationKind" );
    QTest::newRow( "config" ) << QStringLiteral( "config" );
    QTest::newRow( "session" ) << QStringLiteral( "session" );
    QTest::newRow( "crash" ) << QStringLiteral( "crash" );
    QTest::newRow( "manifest-incompatible" ) << QStringLiteral( "manifest-incompatible" );
    QTest::newRow( "manifest-compatible" ) << QStringLiteral( "manifest-compatible" );
}

void StorageMigratorTest::preexistingDestinationIsNeverOverwritten()
{
    QFETCH( QString, destinationKind );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY(
        writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\nnew=config\n" } ) );
    QVERIFY(
        writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nnew=session\n" } ) );
    const QString crashRelative = QStringLiteral( "nested/existing.dmp" );
    QVERIFY( writeBytes( QDir{ fixture.request.legacyCrashDirectory }.filePath( crashRelative ),
                         QByteArray{ "new-crash" } ) );
    const StorageContext targetContext{ fixture.target };
    QString target;
    QByteArray original;
    if ( destinationKind == QStringLiteral( "config" ) ) {
        target = targetContext.configFilePath();
        original = QByteArray{ "[General]\nold=config\n" };
    }
    else if ( destinationKind == QStringLiteral( "session" ) ) {
        target = targetContext.sessionFilePath();
        original = QByteArray{ "[General]\nold=session\n" };
    }
    else if ( destinationKind == QStringLiteral( "crash" ) ) {
        target = QDir{ targetContext.crashesDirectory() }.filePath( crashRelative );
        original = QByteArray{ "old-crash" };
    }
    else if ( destinationKind == QStringLiteral( "manifest-compatible" ) ) {
        target = targetContext.manifestFilePath();
        original = QByteArray{ "[Storage]\nlayoutVersion=1\nproduct=ZzLogg\n" };
    }
    else {
        target = targetContext.manifestFilePath();
        original = QByteArray{ "[Storage]\nlayoutVersion=77\nproduct=Existing\n" };
    }
    QVERIFY( writeBytes( target, original ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QCOMPARE( readBytes( target ), original );
    verifySourceIsActiveWithoutPending( fixture );
}

void StorageMigratorTest::rejectsCrashTreeSymlinkWhenPlatformSupportsIt()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    const QString outside = temporaryDirectory.filePath( QStringLiteral( "outside.dmp" ) );
    const QString link
        = QDir{ fixture.request.legacyCrashDirectory }.filePath( QStringLiteral( "linked.dmp" ) );
    QVERIFY( writeBytes( outside, QByteArray{ "outside" } ) );
    QVERIFY( QDir{}.mkpath( fixture.request.legacyCrashDirectory ) );
    if ( !QFile::link( outside, link ) || !QFileInfo{ link }.isSymLink() ) {
        QSKIP( "platform does not permit creating a detectable file symlink" );
    }

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "symbolic link" ) ) );
    QVERIFY( result.error.contains( link ) );
    verifySourceIsActiveWithoutPending( fixture );
}

void StorageMigratorTest::rollbackFailureReportsBothErrors()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    const QString targetSession = StorageContext{ fixture.target }.sessionFilePath();
    const QString sourceLocator = fixture.source.locatorPath;
    const StorageCopyOperation copy
        = [ targetSession, sourceLocator ]( const QString& source, const QString& target,
                                            QString* error ) {
              if ( QDir::cleanPath( target ) == QDir::cleanPath( targetSession ) ) {
                  writeBytes( sourceLocator, QByteArray{ "not-a-valid-locator" } );
                  if ( error != nullptr ) {
                      *error = QStringLiteral( "original injected failure" );
                  }
                  return false;
              }
              return atomicCopy( source, target, error );
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "original injected failure" ) ) );
    QVERIFY( result.error.contains( QStringLiteral( "rollback" ), Qt::CaseInsensitive ) );
}

void StorageMigratorTest::recoverUsesMigrationLock()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    const QString lockPath = fixture.source.locatorPath + QStringLiteral( ".migration.lock" );
    QLockFile heldLock{ lockPath };
    heldLock.setStaleLockTime( 0 );
    QVERIFY( heldLock.tryLock( 0 ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QDir::cleanPath( lockPath ) ) );
    const auto resolution = fixture.store.resolve();
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->pending.has_value() );
}

void StorageMigratorTest::recoverReadsSourcePendingDespiteProgramLocatorPriority()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    QVERIFY( writeLocator( fixture.store.programLocatorPath(), QStringLiteral( "program" ),
                           fixture.targetRoot ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QSettings sourceLocator{ fixture.store.userLocatorPath(), QSettings::IniFormat };
    QVERIFY( !sourceLocator.contains( QStringLiteral( "Pending/transactionId" ) ) );
    QVERIFY( QFile::exists( fixture.store.programLocatorPath() ) );
}

void StorageMigratorTest::recoverCompletePendingCommitsIdempotently()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    const StorageContext targetContext{ fixture.target };
    QVERIFY2( targetContext.ensureDirectories( &error ), qPrintable( error ) );
    QVERIFY( writeBytes( targetContext.configFilePath(), QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( targetContext.sessionFilePath(), QByteArray{ "[General]\nb=2\n" } ) );
    QVERIFY2( StorageValidator::writeManifest( targetContext, &error ), qPrintable( error ) );
    const StorageMigrator migrator{ fixture.store };

    const StorageMigrationResult first = migrator.recoverPending( fixture.request );
    const StorageMigrationResult second = migrator.recoverPending( fixture.request );

    QVERIFY2( first.success, qPrintable( first.error ) );
    QVERIFY( !first.rolledBack );
    QVERIFY2( second.success, qPrintable( second.error ) );
    QVERIFY( !second.rolledBack );
    const auto resolution = fixture.store.resolve();
    QVERIFY2( resolution.state.has_value(), qPrintable( resolution.error ) );
    QCOMPARE( resolution.state->active.dataRoot, QDir::cleanPath( fixture.targetRoot ) );
    QVERIFY( !resolution.state->pending.has_value() );
}

void StorageMigratorTest::recoverIncompletePendingRollsBackIdempotently()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    const StorageContext targetContext{ fixture.target };
    QVERIFY2( targetContext.ensureDirectories( &error ), qPrintable( error ) );
    QVERIFY( writeBytes( targetContext.configFilePath(), QByteArray{ "[General]\na=1\n" } ) );
    const StorageMigrator migrator{ fixture.store };

    const StorageMigrationResult first = migrator.recoverPending( fixture.request );
    const StorageMigrationResult second = migrator.recoverPending( fixture.request );

    QVERIFY( !first.success );
    QVERIFY( first.rolledBack );
    QVERIFY( !second.success );
    QVERIFY( second.rolledBack );
    verifySourceIsActiveWithoutPending( fixture );
}

QTEST_MAIN( StorageMigratorTest )
#include "storagemigratortest.moc"
