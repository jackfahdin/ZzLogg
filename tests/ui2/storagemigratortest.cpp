#include "storagemigrator.h"

#include "legacystorage.h"
#include "storagevalidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
    void migratesDetectedLegacySessionFallback_data();
    void migratesDetectedLegacySessionFallback();
    void recoversPendingWithDetectedSessionFallback();
    void missingLegacyIniCreatesReadableEmptyTarget_data();
    void missingLegacyIniCreatesReadableEmptyTarget();
    void emptyIniCreationFailureNamesSourceAndTarget();
    void sessionCopyFailureRollsBackAndPreservesPreexistingTargetContent();
    void recursiveCrashCopyUsesInjectedOperation();
    void lockConflictFailsBeforeWritingPendingState();
    void commitFailureCleansManifestAndTransactionFiles();
    void commitFailureWithCommittedTargetRequiresRecovery_data();
    void commitFailureWithCommittedTargetRequiresRecovery();
    void preexistingCompatibleManifestStillCleansCreatedDirectories();
    void cleanupFailureMakesRollbackIncomplete();
    void normalizedPendingDrivesLockTargetAndRecovery();
    void equivalentLegacyPathsRecoverPending();
    void casingOnlyPathsRecoverPendingAccordingToPlatform();
    void rejectsRelativeLegacyPathBeforeCreatingLockDirectory();
    void createsLockParentForValidMigration();
    void reportsLockParentCreationFailureSpecifically();
    void preexistingDestinationIsNeverOverwritten_data();
    void preexistingDestinationIsNeverOverwritten();
    void rejectsPreexistingDestinationSymlink_data();
    void rejectsPreexistingDestinationSymlink();
    void rejectsTargetInsideLegacyCrashBeforePending_data();
    void rejectsTargetInsideLegacyCrashBeforePending();
    void rejectsLegacyIniAliasingTargetOutput_data();
    void rejectsLegacyIniAliasingTargetOutput();
    void rejectsCrashTreeSymlinkWhenPlatformSupportsIt();
    void rollbackFailureReportsBothErrors();
    void recoverUsesMigrationLock();
    void recoverReadsSourcePendingDespiteProgramLocatorPriority();
    void recoverRejectsMismatchedPendingBeforeMutation();
    void recoverRejectsPendingTogetherWithLastMigration();
    void recoverDoesNotInferOutcomeFromSameActiveLocation_data();
    void recoverDoesNotInferOutcomeFromSameActiveLocation();
    void recoverCompletePendingCommitsIdempotently();
    void recoverIncompletePendingRollsBackIdempotently();
};

void StorageMigratorTest::migratesDetectedLegacySessionFallback_data()
{
    QTest::addColumn<bool>( "portable" );
    QTest::addColumn<bool>( "createConfig" );
    QTest::addColumn<bool>( "createPrimarySession" );
    QTest::addColumn<QByteArray>( "primarySessionContents" );
    QTest::addColumn<bool>( "createFallbackSession" );
    QTest::addColumn<QString>( "expectedSessionName" );
    QTest::addColumn<QByteArray>( "expectedSessionContents" );

    const QByteArray config{ "[General]\nfrom=config\n" };
    const QByteArray sessionIni{ "[General]\nfrom=session-ini\n" };
    const QByteArray emptySessionIni{ "[General]\n" };
    const QByteArray sessionConf{ "[General]\nfrom=session-conf\n" };

    QTest::newRow( "portable-config-fallback" )
        << true << true << false << QByteArray{} << false << QStringLiteral( "ZzLogg.conf" )
        << config;
    QTest::newRow( "portable-session-priority" )
        << true << true << true << sessionConf << false
        << QStringLiteral( "ZzLogg_session.conf" ) << sessionConf;
    QTest::newRow( "user-config-fallback" )
        << false << true << false << QByteArray{} << false << QStringLiteral( "ZzLogg.ini" )
        << config;
    QTest::newRow( "user-empty-ini-prefers-conf" )
        << false << true << true << emptySessionIni << true
        << QStringLiteral( "ZzLogg_session.conf" ) << sessionConf;
    QTest::newRow( "user-conf-only" )
        << false << false << false << QByteArray{} << true
        << QStringLiteral( "ZzLogg_session.conf" ) << sessionConf;
    QTest::newRow( "user-nonempty-ini-priority" )
        << false << true << true << sessionIni << true
        << QStringLiteral( "ZzLogg_session.ini" ) << sessionIni;
}

void StorageMigratorTest::migratesDetectedLegacySessionFallback()
{
    QFETCH( bool, portable );
    QFETCH( bool, createConfig );
    QFETCH( bool, createPrimarySession );
    QFETCH( QByteArray, primarySessionContents );
    QFETCH( bool, createFallbackSession );
    QFETCH( QString, expectedSessionName );
    QFETCH( QByteArray, expectedSessionContents );

    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory
        = temporaryDirectory.filePath( QStringLiteral( "application" ) );
    const QString appConfigDirectory
        = temporaryDirectory.filePath( QStringLiteral( "app-config" ) );
    const QString userSettingsDirectory
        = temporaryDirectory.filePath( QStringLiteral( "user-settings" ) );
    const QString oldCrashDirectory
        = temporaryDirectory.filePath( QStringLiteral( "old-crashes" ) );
    const QString legacyDirectory = portable ? applicationDirectory : userSettingsDirectory;
    const QString configName
        = portable ? QStringLiteral( "ZzLogg.conf" ) : QStringLiteral( "ZzLogg.ini" );
    const QString primarySessionName = portable ? QStringLiteral( "ZzLogg_session.conf" )
                                                : QStringLiteral( "ZzLogg_session.ini" );
    const QString configPath = QDir{ legacyDirectory }.filePath( configName );
    const QString primarySessionPath
        = QDir{ legacyDirectory }.filePath( primarySessionName );
    const QString fallbackSessionPath
        = QDir{ userSettingsDirectory }.filePath( QStringLiteral( "ZzLogg_session.conf" ) );
    const QByteArray configContents{ "[General]\nfrom=config\n" };
    const QByteArray fallbackSessionContents{ "[General]\nfrom=session-conf\n" };

    if ( createConfig ) {
        QVERIFY( writeBytes( configPath, configContents ) );
    }
    if ( createPrimarySession ) {
        QVERIFY( writeBytes( primarySessionPath, primarySessionContents ) );
    }
    if ( createFallbackSession ) {
        QVERIFY( writeBytes( fallbackSessionPath, fallbackSessionContents ) );
    }

    const auto detected = LegacyStorageDetector::detect(
        applicationDirectory, userSettingsDirectory, oldCrashDirectory );
    QVERIFY( detected.has_value() );
    const QString expectedSessionPath
        = QDir{ legacyDirectory }.filePath( expectedSessionName );
    QCOMPARE( QDir::cleanPath( detected->sessionFile ), QDir::cleanPath( expectedSessionPath ) );

    StorageLocatorStore store{ applicationDirectory, appConfigDirectory };
    const StorageLocation source{ detected->mode, legacyDirectory,
                                  portable ? store.programLocatorPath()
                                           : store.userLocatorPath(),
                                  false };
    const StorageLocation target{ portable ? StorageMode::UserDirectory
                                            : StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target" ) ),
                                  portable ? store.userLocatorPath()
                                           : store.programLocatorPath(),
                                  false };
    const StorageMigrationRequest request{ QStringLiteral( "legacy-session-fallback" ),
                                           source,
                                           target,
                                           detected->configFile,
                                           detected->sessionFile,
                                           detected->crashDirectory };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );

    const StorageContext targetContext{ target };
    QString copiedSessionSource;
    QString persistedSessionSource;
    const StorageCopyOperation copy
        = [ & ]( const QString& copySource, const QString& copyTarget, QString* copyError ) {
              if ( QDir::cleanPath( copyTarget )
                   == QDir::cleanPath( targetContext.sessionFilePath() ) ) {
                  copiedSessionSource = QDir::cleanPath( copySource );
                  const auto pendingResolution = store.resolve();
                  if ( !pendingResolution.state.has_value()
                       || !pendingResolution.state->pending.has_value() ) {
                      if ( copyError != nullptr ) {
                          *copyError = QStringLiteral( "pending migration was not persisted" );
                      }
                      return false;
                  }
                  persistedSessionSource
                      = QDir::cleanPath( pendingResolution.state->pending->legacySessionFile );
              }
              return atomicCopy( copySource, copyTarget, copyError );
          };

    const StorageMigrationResult result = StorageMigrator{ store, copy }.execute( request );

    QVERIFY2( result.success, qPrintable( result.error ) );
    QCOMPARE( readBytes( targetContext.sessionFilePath() ), expectedSessionContents );
    QCOMPARE( copiedSessionSource, QDir::cleanPath( expectedSessionPath ) );
    QCOMPARE( persistedSessionSource, QDir::cleanPath( expectedSessionPath ) );
}

void StorageMigratorTest::recoversPendingWithDetectedSessionFallback()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory
        = temporaryDirectory.filePath( QStringLiteral( "application" ) );
    const QString appConfigDirectory
        = temporaryDirectory.filePath( QStringLiteral( "app-config" ) );
    const QString userSettingsDirectory
        = temporaryDirectory.filePath( QStringLiteral( "user-settings" ) );
    const QString legacyConfig
        = QDir{ userSettingsDirectory }.filePath( QStringLiteral( "ZzLogg.ini" ) );
    const QByteArray configContents{ "[General]\nrestored=session-fallback\n" };
    QVERIFY( writeBytes( legacyConfig, configContents ) );

    const auto detected = LegacyStorageDetector::detect(
        applicationDirectory, userSettingsDirectory,
        temporaryDirectory.filePath( QStringLiteral( "old-crashes" ) ) );
    QVERIFY( detected.has_value() );
    QCOMPARE( QDir::cleanPath( detected->sessionFile ), QDir::cleanPath( legacyConfig ) );

    StorageLocatorStore store{ applicationDirectory, appConfigDirectory };
    const StorageLocation source{ StorageMode::UserDirectory, userSettingsDirectory,
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{ QStringLiteral( "recover-session-fallback" ),
                                           source,
                                           target,
                                           detected->configFile,
                                           detected->sessionFile,
                                           detected->crashDirectory };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    const auto pendingResolution = store.resolve();
    QVERIFY( pendingResolution.state.has_value() );
    QVERIFY( pendingResolution.state->pending.has_value() );
    const StorageMigrationRequest persisted = *pendingResolution.state->pending;
    QCOMPARE( QDir::cleanPath( persisted.legacySessionFile ), QDir::cleanPath( legacyConfig ) );

    const StorageContext targetContext{ target };
    QVERIFY2( targetContext.ensureDirectories( &error ), qPrintable( error ) );
    QVERIFY( atomicCopy( persisted.legacyConfigFile, targetContext.configFilePath(), &error ) );
    QVERIFY( atomicCopy( persisted.legacySessionFile, targetContext.sessionFilePath(), &error ) );
    QVERIFY2( StorageValidator::writeManifest( targetContext, &error ), qPrintable( error ) );

    const StorageMigrationResult result = StorageMigrator{ store }.recoverPending( persisted );

    QVERIFY2( result.success, qPrintable( result.error ) );
    QCOMPARE( readBytes( targetContext.sessionFilePath() ), configContents );
    const auto committed = store.resolve();
    QVERIFY( committed.state.has_value() );
    QVERIFY( !committed.state->pending.has_value() );
    QCOMPARE( committed.state->active.dataRoot, QDir::cleanPath( target.dataRoot ) );
}

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
    QVERIFY( result.error.contains( QStringLiteral( "already held" ), Qt::CaseInsensitive ) );
    QVERIFY( result.error.contains( QDir::cleanPath( lockPath ) ) );
    verifySourceIsActiveWithoutPending( fixture );
    QVERIFY( !QFile::exists( fixture.targetRoot ) );
}

void StorageMigratorTest::commitFailureCleansManifestAndTransactionFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    QVERIFY( writeBytes( QDir{ fixture.request.legacyCrashDirectory }.filePath(
                             QStringLiteral( "nested/dump.dmp" ) ),
                         QByteArray{ "dump" } ) );
    bool blockerCreated = false;
    const StorageCopyOperation copy = [ &fixture, &blockerCreated ]( const QString& source,
                                                                     const QString& target,
                                                                     QString* error ) {
        if ( !blockerCreated ) {
            blockerCreated
                = writeBytes( fixture.applicationDirectory, QByteArray{ "blocks locator parent" } );
        }
        return atomicCopy( source, target, error );
    };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( blockerCreated );
    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "commit" ) ) );
    QVERIFY( !QFileInfo{ fixture.targetRoot }.exists() );
    QSettings sourceLocator{ fixture.store.userLocatorPath(), QSettings::IniFormat };
    QCOMPARE( sourceLocator.value( QStringLiteral( "Storage/dataRoot" ) ).toString(),
              QDir::cleanPath( fixture.source.dataRoot ) );
    QVERIFY( !sourceLocator.contains( QStringLiteral( "Pending/transactionId" ) ) );
    QCOMPARE( sourceLocator.value( QStringLiteral( "LastMigration/outcome" ) ).toString(),
              QStringLiteral( "rolledBack" ) );
}

void StorageMigratorTest::commitFailureWithCommittedTargetRequiresRecovery_data()
{
    QTest::addColumn<QString>( "verifiedValue" );
    QTest::addColumn<bool>( "expectInvalid" );
    QTest::addColumn<bool>( "expectGenericRollback" );
    QTest::newRow( "verified-false" ) << QStringLiteral( "false" ) << false << false;
    QTest::newRow( "verified-missing" ) << QString{} << true << false;
    QTest::newRow( "verified-invalid" ) << QStringLiteral( "sometimes" ) << true << false;
    QTest::newRow( "verified-true" ) << QStringLiteral( "true" ) << false << true;
}

void StorageMigratorTest::commitFailureWithCommittedTargetRequiresRecovery()
{
    QFETCH( QString, verifiedValue );
    QFETCH( bool, expectInvalid );
    QFETCH( bool, expectGenericRollback );
#ifndef Q_OS_WIN
    QSKIP( "the deterministic delete/restore double-fault fixture uses Windows share modes" );
#else
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const QByteArray config{ "[General]\na=1\n" };
    const QByteArray session{ "[General]\nb=2\n" };
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, config ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, session ) );
    const QByteArray previousTarget( 128 * 1024 * 1024, 'x' );
    QVERIFY( writeBytes( fixture.target.locatorPath, previousTarget ) );

    std::atomic<HANDLE> sourceHandle{ INVALID_HANDLE_VALUE };
    std::atomic_bool executeFinished{ false };
    std::atomic_bool sawCommittedTarget{ false };
    std::atomic_bool changedVerified{ false };
    std::atomic_bool lockedCommittedTarget{ false };
    std::thread faultThread;
    const StorageCopyOperation copy = [ & ]( const QString& source, const QString& target,
                                             QString* error ) {
        if ( sourceHandle.load() == INVALID_HANDLE_VALUE ) {
            const HANDLE handle
                = CreateFileW( reinterpret_cast<LPCWSTR>( fixture.source.locatorPath.utf16() ),
                               GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( handle == INVALID_HANDLE_VALUE ) {
                if ( error != nullptr ) {
                    *error = QStringLiteral( "failed to lock source locator fixture" );
                }
                return false;
            }
            sourceHandle.store( handle );
            faultThread = std::thread( [ & ] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
                while ( !executeFinished.load() && std::chrono::steady_clock::now() < deadline ) {
                    const QByteArray locator = readBytes( fixture.target.locatorPath );
                    if ( locator.contains( "LastMigration" ) && locator.contains( "committed" )
                         && locator.contains( fixture.request.transactionId.toUtf8() ) ) {
                        sawCommittedTarget.store( true );
                        {
                            QSettings settings{ fixture.target.locatorPath, QSettings::IniFormat };
                            if ( verifiedValue.isEmpty() ) {
                                settings.remove( QStringLiteral( "Storage/verified" ) );
                            }
                            else {
                                settings.setValue( QStringLiteral( "Storage/verified" ),
                                                   verifiedValue );
                            }
                            settings.sync();
                            if ( settings.status() != QSettings::NoError ) {
                                break;
                            }
                            changedVerified.store( true );
                        }
                        const HANDLE targetHandle = CreateFileW(
                            reinterpret_cast<LPCWSTR>( fixture.target.locatorPath.utf16() ),
                            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
                        if ( targetHandle != INVALID_HANDLE_VALUE ) {
                            lockedCommittedTarget.store( true );
                            while ( !executeFinished.load() ) {
                                std::this_thread::yield();
                            }
                            CloseHandle( targetHandle );
                            const HANDLE heldSource = sourceHandle.exchange( INVALID_HANDLE_VALUE );
                            if ( heldSource != INVALID_HANDLE_VALUE ) {
                                CloseHandle( heldSource );
                            }
                            return;
                        }
                    }
                    std::this_thread::yield();
                }
                const HANDLE heldSource = sourceHandle.exchange( INVALID_HANDLE_VALUE );
                if ( heldSource != INVALID_HANDLE_VALUE ) {
                    CloseHandle( heldSource );
                }
            } );
        }
        return atomicCopy( source, target, error );
    };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );
    executeFinished.store( true );
    if ( faultThread.joinable() ) {
        faultThread.join();
    }
    const HANDLE heldSource = sourceHandle.exchange( INVALID_HANDLE_VALUE );
    if ( heldSource != INVALID_HANDLE_VALUE ) {
        CloseHandle( heldSource );
    }

    QVERIFY( sawCommittedTarget.load() );
    QVERIFY( changedVerified.load() );
    QVERIFY( lockedCommittedTarget.load() );
    QVERIFY( !result.success );
    QSettings sourceLocator{ fixture.source.locatorPath, QSettings::IniFormat };
    QSettings targetLocator{ fixture.target.locatorPath, QSettings::IniFormat };
    QCOMPARE( targetLocator.value( QStringLiteral( "LastMigration/transactionId" ) ).toString(),
              fixture.request.transactionId );
    QCOMPARE( targetLocator.value( QStringLiteral( "LastMigration/outcome" ) ).toString(),
              QStringLiteral( "committed" ) );
    const StorageContext targetContext{ fixture.target };
    if ( expectGenericRollback ) {
        QVERIFY( !result.rolledBack );
        QVERIFY( result.error.contains( QStringLiteral( "rollback failed" ) ) );
        QCOMPARE( sourceLocator.value( QStringLiteral( "Pending/transactionId" ) ).toString(),
                  fixture.request.transactionId );
        QVERIFY( !QFileInfo{ fixture.targetRoot }.exists() );
        QVERIFY( !result.error.contains(
            QStringLiteral( "target locator remains committed; recovery required" ) ) );
    }
    else {
        QVERIFY( !result.rolledBack );
        QCOMPARE( sourceLocator.value( QStringLiteral( "Pending/transactionId" ) ).toString(),
                  fixture.request.transactionId );
        QCOMPARE( readBytes( targetContext.configFilePath() ), config );
        QCOMPARE( readBytes( targetContext.sessionFilePath() ), session );
        QVERIFY( StorageValidator::hasCompatibleManifest( fixture.targetRoot ) );
        QVERIFY( result.error.contains( QStringLiteral( "recovery required" ) ) );
        QVERIFY( result.error.contains( fixture.target.locatorPath ) );
        if ( expectInvalid ) {
            QVERIFY( result.error.contains( QStringLiteral( "target locator invalid" ) ) );
            QVERIFY( result.error.contains( QStringLiteral( "verified" ) ) );
        }
        else {
            QVERIFY(
                result.error.contains( QStringLiteral( "target locator remains committed" ) ) );
        }
    }
#endif
}

void StorageMigratorTest::preexistingCompatibleManifestStillCleansCreatedDirectories()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const StorageContext targetContext{ fixture.target };
    QString error;
    QVERIFY2( StorageValidator::writeManifest( targetContext, &error ), qPrintable( error ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( QFileInfo{ targetContext.manifestFilePath() }.isFile() );
    QVERIFY( !QFileInfo{ targetContext.configDirectory() }.exists() );
    QVERIFY( !QFileInfo{ targetContext.sessionDirectory() }.exists() );
    QVERIFY( !QFileInfo{ targetContext.logsDirectory() }.exists() );
    QVERIFY( !QFileInfo{ targetContext.crashesDirectory() }.exists() );
}

void StorageMigratorTest::cleanupFailureMakesRollbackIncomplete()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    const QString targetConfig = StorageContext{ fixture.target }.configFilePath();
    const StorageCopyOperation copy
        = [ targetConfig ]( const QString&, const QString& target, QString* error ) {
              if ( QDir::cleanPath( target ) == QDir::cleanPath( targetConfig ) ) {
                  QDir{}.mkpath( target );
                  if ( error != nullptr ) {
                      *error = QStringLiteral( "injected copy failure after directory creation" );
                  }
                  return false;
              }
              return true;
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "injected copy failure" ) ) );
    QVERIFY( result.error.contains( targetConfig ) );
    QVERIFY( result.error.contains( QStringLiteral( "cleanup" ), Qt::CaseInsensitive ) );
    QVERIFY( QFileInfo{ targetConfig }.isDir() );
    verifySourceIsActiveWithoutPending( fixture );
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

void StorageMigratorTest::equivalentLegacyPathsRecoverPending()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    StorageMigrationRequest dotted = fixture.request;
    dotted.legacyConfigFile
        = QDir{ fixture.legacyDirectory }.filePath( QStringLiteral( "folder/../ZzLogg.ini" ) );
    dotted.legacySessionFile = QDir{ fixture.legacyDirectory }.filePath(
        QStringLiteral( "folder/../ZzLogg_session.ini" ) );
    dotted.legacyCrashDirectory
        = QDir{ fixture.legacyDirectory }.filePath( QStringLiteral( "folder/../klogg_dump" ) );
    QString error;
    QVERIFY2( fixture.store.writePending( dotted, &error ), qPrintable( error ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( fixture.request );

    QVERIFY( !result.success );
    QVERIFY2( result.rolledBack, qPrintable( result.error ) );
    verifySourceIsActiveWithoutPending( fixture );
}

void StorageMigratorTest::casingOnlyPathsRecoverPendingAccordingToPlatform()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    StorageMigrationRequest casingOnly = fixture.request;
    casingOnly.source.locatorPath = casingOnly.source.locatorPath.toUpper();
    casingOnly.target.locatorPath = casingOnly.target.locatorPath.toUpper();
    casingOnly.source.dataRoot = casingOnly.source.dataRoot.toUpper();
    casingOnly.target.dataRoot = casingOnly.target.dataRoot.toUpper();
    casingOnly.legacyConfigFile = casingOnly.legacyConfigFile.toUpper();
    casingOnly.legacySessionFile = casingOnly.legacySessionFile.toUpper();
    casingOnly.legacyCrashDirectory = casingOnly.legacyCrashDirectory.toUpper();

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( casingOnly );

#ifdef Q_OS_WIN
    QVERIFY( !result.success );
    QVERIFY2( result.rolledBack, qPrintable( result.error ) );
    verifySourceIsActiveWithoutPending( fixture );
#else
    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    const auto resolution = fixture.store.resolve();
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->pending.has_value() );
#endif
}

void StorageMigratorTest::rejectsRelativeLegacyPathBeforeCreatingLockDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    fixture.request.source.locatorPath.clear();
    fixture.request.legacyConfigFile = QStringLiteral( "relative.ini" );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "absolute" ) ) );
    QVERIFY( !QFileInfo{ fixture.appConfigDirectory }.exists() );
}

void StorageMigratorTest::createsLockParentForValidMigration()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    fixture.request.source.locatorPath.clear();
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY2( result.success, qPrintable( result.error ) );
    QVERIFY( QFileInfo{ fixture.appConfigDirectory }.isDir() );
}

void StorageMigratorTest::reportsLockParentCreationFailureSpecifically()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    fixture.request.source.locatorPath.clear();
    QVERIFY( writeBytes( fixture.appConfigDirectory, QByteArray{ "blocks-directory" } ) );
    const QString lockPath = fixture.store.userLocatorPath() + QStringLiteral( ".migration.lock" );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( lockPath ) );
    QVERIFY( result.error.contains( QStringLiteral( "directory" ), Qt::CaseInsensitive ) );
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

void StorageMigratorTest::rejectsPreexistingDestinationSymlink_data()
{
    QTest::addColumn<QString>( "destinationKind" );
    QTest::newRow( "config-link" ) << QStringLiteral( "config" );
    QTest::newRow( "session-link" ) << QStringLiteral( "session" );
    QTest::newRow( "crash-link" ) << QStringLiteral( "crash" );
    QTest::newRow( "manifest-link" ) << QStringLiteral( "manifest" );
}

void StorageMigratorTest::rejectsPreexistingDestinationSymlink()
{
    QFETCH( QString, destinationKind );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    QVERIFY( writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    const QString crashRelative = QStringLiteral( "nested/existing.dmp" );
    QVERIFY( writeBytes( QDir{ fixture.request.legacyCrashDirectory }.filePath( crashRelative ),
                         QByteArray{ "dump" } ) );
    const StorageContext targetContext{ fixture.target };
    QString destination;
    if ( destinationKind == QStringLiteral( "config" ) ) {
        destination = targetContext.configFilePath();
    }
    else if ( destinationKind == QStringLiteral( "session" ) ) {
        destination = targetContext.sessionFilePath();
    }
    else if ( destinationKind == QStringLiteral( "crash" ) ) {
        destination = QDir{ targetContext.crashesDirectory() }.filePath( crashRelative );
    }
    else {
        destination = targetContext.manifestFilePath();
    }
    const QString linkSource = temporaryDirectory.filePath( QStringLiteral( "link-source" ) );
    QVERIFY( writeBytes( linkSource, QByteArray{ "link-source" } ) );
    QVERIFY( QDir{}.mkpath( QFileInfo{ destination }.absolutePath() ) );
    if ( !QFile::link( linkSource, destination ) || !QFileInfo{ destination }.isSymLink() ) {
        QSKIP( "platform does not permit creating a detectable destination symlink" );
    }
    QVERIFY( QFile::remove( linkSource ) );
    QVERIFY( QFileInfo{ destination }.isSymLink() );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( result.rolledBack );
    QVERIFY( result.error.contains( destination ) );
    QVERIFY( QFileInfo{ destination }.isSymLink() );
}

void StorageMigratorTest::rejectsTargetInsideLegacyCrashBeforePending_data()
{
    QTest::addColumn<bool>( "useChild" );
    QTest::newRow( "same-root" ) << false;
    QTest::newRow( "child-root" ) << true;
}

void StorageMigratorTest::rejectsTargetInsideLegacyCrashBeforePending()
{
    QFETCH( bool, useChild );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    fixture.request.target.dataRoot
        = useChild
              ? QDir{ fixture.request.legacyCrashDirectory }.filePath( QStringLiteral( "child" ) )
              : fixture.request.legacyCrashDirectory;
    const QByteArray originalLocator = readBytes( fixture.store.userLocatorPath() );
    int copyCalls = 0;
    const StorageCopyOperation copy
        = [ &copyCalls ]( const QString&, const QString&, QString* error ) {
              ++copyCalls;
              if ( error != nullptr ) {
                  *error = QStringLiteral( "copy should not be reached" );
              }
              return false;
          };

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store, copy }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QCOMPARE( copyCalls, 0 );
    QVERIFY( result.error.contains( QDir::cleanPath( fixture.request.legacyCrashDirectory ) ) );
    QCOMPARE( readBytes( fixture.store.userLocatorPath() ), originalLocator );
    QVERIFY( !QFileInfo{ fixture.request.target.dataRoot }.exists() );
}

void StorageMigratorTest::rejectsLegacyIniAliasingTargetOutput_data()
{
    QTest::addColumn<bool>( "configAliases" );
    QTest::newRow( "config-alias" ) << true;
    QTest::newRow( "session-alias" ) << false;
}

void StorageMigratorTest::rejectsLegacyIniAliasingTargetOutput()
{
    QFETCH( bool, configAliases );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const StorageContext targetContext{ fixture.target };
    const QString conflict
        = configAliases ? targetContext.configFilePath() : targetContext.sessionFilePath();
    if ( configAliases ) {
        fixture.request.legacyConfigFile = conflict;
        QVERIFY(
            writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
    }
    else {
        fixture.request.legacySessionFile = conflict;
        QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
    }
    const QByteArray originalLocator = readBytes( fixture.store.userLocatorPath() );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.execute( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( conflict ) );
    QCOMPARE( readBytes( fixture.store.userLocatorPath() ), originalLocator );
    QVERIFY( !QFileInfo{ fixture.targetRoot }.exists() );
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

void StorageMigratorTest::recoverRejectsMismatchedPendingBeforeMutation()
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
    StorageMigrationRequest mismatched = fixture.request;
    mismatched.legacySessionFile
        = temporaryDirectory.filePath( QStringLiteral( "different-session.ini" ) );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( mismatched );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "does not match" ) ) );
    QVERIFY( !result.error.contains( QStringLiteral( "rollback" ), Qt::CaseInsensitive ) );
    const auto resolution = fixture.store.resolve();
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->pending.has_value() );
}

void StorageMigratorTest::recoverRejectsPendingTogetherWithLastMigration()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    QString error;
    QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
    {
        QSettings settings{ fixture.source.locatorPath, QSettings::IniFormat };
        settings.setValue( QStringLiteral( "LastMigration/transactionId" ),
                           fixture.request.transactionId );
        settings.setValue( QStringLiteral( "LastMigration/outcome" ),
                           QStringLiteral( "rolledBack" ) );
        settings.sync();
        QCOMPARE( settings.status(), QSettings::NoError );
    }
    const QByteArray ambiguous = readBytes( fixture.source.locatorPath );

    const StorageMigrationResult result
        = StorageMigrator{ fixture.store }.recoverPending( fixture.request );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "Pending" ) ) );
    QVERIFY( result.error.contains( QStringLiteral( "LastMigration" ) ) );
    QCOMPARE( readBytes( fixture.source.locatorPath ), ambiguous );
}

void StorageMigratorTest::recoverDoesNotInferOutcomeFromSameActiveLocation_data()
{
    QTest::addColumn<bool>( "commitOutcome" );
    QTest::newRow( "unrelated-after-commit" ) << true;
    QTest::newRow( "unrelated-after-rollback" ) << false;
}

void StorageMigratorTest::recoverDoesNotInferOutcomeFromSameActiveLocation()
{
    QFETCH( bool, commitOutcome );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    MigrationFixture fixture{ temporaryDirectory };
    QVERIFY( fixture.initializeLocator() );
    const StorageMigrator migrator{ fixture.store };
    if ( commitOutcome ) {
        QVERIFY( writeBytes( fixture.request.legacyConfigFile, QByteArray{ "[General]\na=1\n" } ) );
        QVERIFY(
            writeBytes( fixture.request.legacySessionFile, QByteArray{ "[General]\nb=2\n" } ) );
        const StorageMigrationResult executed = migrator.execute( fixture.request );
        QVERIFY2( executed.success, qPrintable( executed.error ) );
    }
    else {
        QString error;
        QVERIFY2( fixture.store.writePending( fixture.request, &error ), qPrintable( error ) );
        const StorageMigrationResult rolledBack = migrator.recoverPending( fixture.request );
        QVERIFY( rolledBack.rolledBack );
    }
    StorageMigrationRequest unrelated = fixture.request;
    unrelated.transactionId = QStringLiteral( "unrelated-transaction" );

    const StorageMigrationResult result = migrator.recoverPending( unrelated );

    QVERIFY( !result.success );
    QVERIFY( !result.rolledBack );
    QVERIFY( result.error.contains( QStringLiteral( "transaction" ), Qt::CaseInsensitive ) );
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
