#include "storagelocator.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

QString normalized( const QString& path )
{
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool writeLocator( const QString& path, const QString& mode, const QString& dataRoot,
                   const QString& extraStorageFields = {} )
{
    QDir{}.mkpath( QFileInfo{ path }.absolutePath() );
    QSaveFile locator{ path };
    if ( !locator.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        return false;
    }
    const QByteArray contents
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=%1\ndataRoot=%2\nverified=true\n%3" )
              .arg( mode, dataRoot, extraStorageFields )
              .toUtf8();
    return locator.write( contents ) == contents.size() && locator.commit();
}

bool writeRawLocator( const QString& path, const QByteArray& contents )
{
    QDir{}.mkpath( QFileInfo{ path }.absolutePath() );
    QSaveFile locator{ path };
    return locator.open( QIODevice::WriteOnly ) && locator.write( contents ) == contents.size()
           && locator.commit();
}

QByteArray readBytes( const QString& path )
{
    QFile file{ path };
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return file.readAll();
}

} // namespace

class StorageLocatorTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void usesFixedLocatorPaths();
    void resolvesCommandLineBeforeProgramAndUserLocators();
    void resolvesProgramRelativeDataDirectory();
    void fallsBackToUserLocatorWhenProgramLocatorIsAbsent();
    void reportsMalformedHigherPriorityLocatorWithoutFallback();
    void writeActiveRejectsConflictingLocatorChange();
    void writeActiveLeavesExistingLocatorWhenReplacementRejected();
    void writeActiveRemovesLowerPriorityLocatorWhenProgramWins();
    void programWriteDoesNotCreateAppConfigDirectory();
    void writeActiveRejectsCommandLineOverrideWithoutChangingLocators();
    void writeActiveRejectsConflictingExplicitLocatorPath();
    void writePendingPreservesVerifiedSourceState();
    void writePendingRejectsStaleSourceState();
    void writePendingRejectsDifferentExistingPending();
    void rollbackPendingRemovesNewLegacyLocator();
    void locatorMutationLockBlocksWrites();
    void staleMalformedMutationLockIsRecovered();
    void oldPendingWithoutPreimageRollsBackSyntheticLocator();
    void discardsUnverifiedRolledBackLegacyLocator();
    void writeActiveRejectsReplacingDifferentActiveLocation();
    void commitPendingActivatesUnverifiedTargetAndRemovesSource();
    void commitPendingRestoresTargetWhenSourceDeletionFails();
    void commitPendingRejectsMismatchedRequest();
    void rollbackPendingRestoresSourceAndKeepsTargetLocator();
    void rollbackPendingRejectsMismatchedRequest();
    void writePendingRejectsCommandLineLocation();
    void writePendingRejectsUserLocationAtProgramLocator();
    void rejectsIncompletePendingGroup();
    void rejectsPendingWhoseSourceDiffersFromActive();
    void rejectsIncompleteLastMigrationGroup();
    void rejectsPendingTogetherWithLastMigration();
    void roundTripsCompleteProgramRelativePending();
    void readsPendingWithoutSourceLogsAsLegacyCompatible();
};

void StorageLocatorTest::usesFixedLocatorPaths()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "app" ) );
    const QString configDirectory = temporaryDirectory.filePath( QStringLiteral( "config" ) );
    const StorageLocatorStore store{ applicationDirectory, configDirectory };

    QCOMPARE( store.programLocatorPath(),
              normalized( applicationDirectory + QStringLiteral( "/ZzLogg.storage.ini" ) ) );
    QCOMPARE( store.userLocatorPath(),
              normalized( configDirectory + QStringLiteral( "/storage.ini" ) ) );
}

void StorageLocatorTest::resolvesCommandLineBeforeProgramAndUserLocators()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "app" ) );
    const QString configDirectory = temporaryDirectory.filePath( QStringLiteral( "config" ) );
    const StorageLocatorStore store{ applicationDirectory, configDirectory };
    const QString programRoot = temporaryDirectory.filePath( QStringLiteral( "program-root" ) );
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    const QString cliRoot = temporaryDirectory.filePath( QStringLiteral( "cli-root" ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ), programRoot ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ), userRoot ) );

    const auto resolution = store.resolve( cliRoot );

    QCOMPARE( resolution.source, StorageResolutionSource::CommandLine );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( cliRoot ) );
    QVERIFY( resolution.state->active.commandLineOverride );
    QVERIFY( !QFile::exists( cliRoot ) );
}

void StorageLocatorTest::resolvesProgramRelativeDataDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "app" ) );
    const StorageLocatorStore store{ applicationDirectory,
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           QStringLiteral( "data" ) ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot,
              normalized( applicationDirectory + QStringLiteral( "/data" ) ) );
    QCOMPARE( resolution.state->active.locatorPath, store.programLocatorPath() );
}

void StorageLocatorTest::fallsBackToUserLocatorWhenProgramLocatorIsAbsent()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "custom" ), userRoot ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::UserLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( userRoot ) );
    QCOMPARE( resolution.state->active.mode, StorageMode::CustomDirectory );
}

void StorageLocatorTest::reportsMalformedHigherPriorityLocatorWithoutFallback()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           QStringLiteral( "data" ), QStringLiteral( "formatVersion=7\n" ) ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ), userRoot ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( !resolution.state.has_value() );
    QVERIFY( !resolution.error.isEmpty() );
}

void StorageLocatorTest::writeActiveRejectsConflictingLocatorChange()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    const QString programRoot = temporaryDirectory.filePath( QStringLiteral( "program-root" ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ), userRoot ) );

    QString error;
    QVERIFY(
        !store.writeActive( { StorageMode::ProgramDirectory, programRoot, {}, false }, &error ) );

    QVERIFY( error.contains( QStringLiteral( "active" ), Qt::CaseInsensitive ) );
    QVERIFY( !QFile::exists( store.programLocatorPath() ) );
    QVERIFY( QFile::exists( store.userLocatorPath() ) );
    const auto resolution = store.resolve();
    QCOMPARE( resolution.source, StorageResolutionSource::UserLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( userRoot ) );
}

void StorageLocatorTest::writeActiveLeavesExistingLocatorWhenReplacementRejected()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString previousProgramRoot
        = temporaryDirectory.filePath( QStringLiteral( "previous-program-root" ) );
    const QString replacementProgramRoot
        = temporaryDirectory.filePath( QStringLiteral( "replacement-program-root" ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           previousProgramRoot ) );
    const QByteArray originalProgramLocator = readBytes( store.programLocatorPath() );
    QVERIFY( !originalProgramLocator.isEmpty() );
    QVERIFY( QDir{}.mkpath( store.userLocatorPath() ) );

    QString error;
    QVERIFY( !store.writeActive(
        { StorageMode::ProgramDirectory, replacementProgramRoot, {}, false }, &error ) );

    QVERIFY( !error.isEmpty() );
    QCOMPARE( readBytes( store.programLocatorPath() ), originalProgramLocator );
    QVERIFY( QDir{ store.userLocatorPath() }.exists() );
}

void StorageLocatorTest::writeActiveRemovesLowerPriorityLocatorWhenProgramWins()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString programRoot = temporaryDirectory.filePath( QStringLiteral( "program-root" ) );
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ), programRoot ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ), userRoot ) );

    QString error;
    QVERIFY2( store.writeActive(
                  { StorageMode::ProgramDirectory, programRoot, store.programLocatorPath(), false },
                  &error ),
              qPrintable( error ) );

    QVERIFY( QFileInfo::exists( store.programLocatorPath() ) );
    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
    const auto resolution = store.resolve();
    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( programRoot ) );
}

void StorageLocatorTest::programWriteDoesNotCreateAppConfigDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory
        = temporaryDirectory.filePath( QStringLiteral( "portable-app" ) );
    const QString appConfigDirectory
        = temporaryDirectory.filePath( QStringLiteral( "must-not-be-created/config" ) );
    QVERIFY( QDir{}.mkpath( applicationDirectory ) );
    const StorageLocatorStore store{ applicationDirectory, appConfigDirectory };

    QString error;
    QVERIFY2(
        store.writeActive( { StorageMode::ProgramDirectory,
                             QDir{ applicationDirectory }.filePath( QStringLiteral( "data" ) ),
                             store.programLocatorPath(), false },
                           &error ),
        qPrintable( error ) );

    QVERIFY( QFileInfo::exists( store.programLocatorPath() ) );
    QVERIFY( !QFileInfo::exists( appConfigDirectory ) );
}

void StorageLocatorTest::writeActiveRejectsCommandLineOverrideWithoutChangingLocators()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           temporaryDirectory.filePath( QStringLiteral( "program-root" ) ) ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ),
                           temporaryDirectory.filePath( QStringLiteral( "user-root" ) ) ) );
    const QByteArray originalProgram = readBytes( store.programLocatorPath() );
    const QByteArray originalUser = readBytes( store.userLocatorPath() );
    const auto commandLine
        = store.resolve( temporaryDirectory.filePath( QStringLiteral( "cli-root" ) ) );
    QVERIFY( commandLine.state.has_value() );
    QVERIFY( commandLine.state->active.commandLineOverride );

    QString error;
    QVERIFY( !store.writeActive( commandLine.state->active, &error ) );

    QVERIFY( error.contains( QStringLiteral( "command-line" ) ) );
    QCOMPARE( readBytes( store.programLocatorPath() ), originalProgram );
    QCOMPARE( readBytes( store.userLocatorPath() ), originalUser );
}

void StorageLocatorTest::writeActiveRejectsConflictingExplicitLocatorPath()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           temporaryDirectory.filePath( QStringLiteral( "program-root" ) ) ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ),
                           temporaryDirectory.filePath( QStringLiteral( "user-root" ) ) ) );
    const QByteArray originalProgram = readBytes( store.programLocatorPath() );
    const QByteArray originalUser = readBytes( store.userLocatorPath() );

    QString error;
    QVERIFY(
        !store.writeActive( { StorageMode::ProgramDirectory,
                              temporaryDirectory.filePath( QStringLiteral( "new-program-root" ) ),
                              store.userLocatorPath(), false },
                            &error ) );

    QVERIFY( error.contains( QStringLiteral( "locator" ) ) );
    QCOMPARE( readBytes( store.programLocatorPath() ), originalProgram );
    QCOMPARE( readBytes( store.userLocatorPath() ), originalUser );
}

void StorageLocatorTest::writePendingPreservesVerifiedSourceState()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    const StorageMigrationRequest request{
        QStringLiteral( "pending-transaction" ),
        source,
        target,
        temporaryDirectory.filePath( QStringLiteral( "legacy.ini" ) ),
        temporaryDirectory.filePath( QStringLiteral( "session.ini" ) ),
        temporaryDirectory.filePath( QStringLiteral( "logs" ) )
    };

    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::UserLocator );
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->verified );
    QCOMPARE( resolution.state->active.dataRoot, normalized( source.dataRoot ) );
    QVERIFY( resolution.state->pending.has_value() );
    QCOMPARE( resolution.state->pending->transactionId, request.transactionId );
}

void StorageLocatorTest::writePendingRejectsStaleSourceState()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation active{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "active-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation stale{ StorageMode::UserDirectory,
                                 temporaryDirectory.filePath( QStringLiteral( "stale-root" ) ),
                                 store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    QString error;
    QVERIFY2( store.writeActive( active, &error ), qPrintable( error ) );

    QVERIFY( !store.writePending( { QStringLiteral( "stale-source" ), stale, target, {}, {}, {} },
                                  &error ) );

    QVERIFY( error.contains( QStringLiteral( "active" ), Qt::CaseInsensitive ) );
    const auto resolution = store.resolve();
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( active.dataRoot ) );
    QVERIFY( !resolution.state->pending.has_value() );
}

void StorageLocatorTest::writePendingRejectsDifferentExistingPending()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageMigrationRequest first{ QStringLiteral( "first-pending" ),
                                         source,
                                         { StorageMode::ProgramDirectory,
                                           temporaryDirectory.filePath(
                                               QStringLiteral( "first-target" ) ),
                                           store.programLocatorPath(), false },
                                         {},
                                         {},
                                         {} };
    StorageMigrationRequest second = first;
    second.transactionId = QStringLiteral( "second-pending" );
    second.target.dataRoot = temporaryDirectory.filePath( QStringLiteral( "second-target" ) );
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( first, &error ), qPrintable( error ) );

    QVERIFY( !store.writePending( second, &error ) );

    QVERIFY( error.contains( QStringLiteral( "pending" ), Qt::CaseInsensitive ) );
    const auto resolution = store.resolve();
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->pending.has_value() );
    QCOMPARE( resolution.state->pending->transactionId, first.transactionId );
}

void StorageLocatorTest::rollbackPendingRemovesNewLegacyLocator()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation legacySource{ StorageMode::UserDirectory,
                                        temporaryDirectory.filePath(
                                            QStringLiteral( "legacy-settings" ) ),
                                        store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "new-legacy-locator" ), legacySource, target, {}, {}, {}
    };
    QString error;
    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    QVERIFY( QFileInfo::exists( store.userLocatorPath() ) );

    QVERIFY2( store.rollbackPending( request, &error ), qPrintable( error ) );

    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
    QCOMPARE( store.resolve().source, StorageResolutionSource::Missing );
}

void StorageLocatorTest::locatorMutationLockBlocksWrites()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    QDir{}.mkpath( QFileInfo{ store.mutationLockPath() }.absolutePath() );
    QLockFile lock{ store.mutationLockPath() };
    lock.setStaleLockTime( 0 );
    QVERIFY( lock.tryLock( 0 ) );
    QString error;

    QVERIFY( !store.writeActive( { StorageMode::UserDirectory,
                                   temporaryDirectory.filePath( QStringLiteral( "data" ) ),
                                   store.userLocatorPath(), false },
                                 &error ) );

    QVERIFY( error.contains( QStringLiteral( "lock" ), Qt::CaseInsensitive ) );
    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::staleMalformedMutationLockIsRecovered()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    QVERIFY( QDir{}.mkpath( QFileInfo{ store.mutationLockPath() }.absolutePath() ) );
    QFile staleLock{ store.mutationLockPath() };
    QVERIFY( staleLock.open( QIODevice::WriteOnly ) );
    staleLock.close();
    QVERIFY( staleLock.open( QIODevice::ReadWrite ) );
    QVERIFY( staleLock.setFileTime( QDateTime::currentDateTimeUtc().addSecs( -60 ),
                                    QFileDevice::FileModificationTime ) );
    staleLock.close();

    QString error;
    QVERIFY2( store.writeActive( { StorageMode::UserDirectory,
                                   temporaryDirectory.filePath( QStringLiteral( "data" ) ),
                                   store.userLocatorPath(), false },
                                 &error ),
              qPrintable( error ) );

    QVERIFY( QFileInfo::exists( store.userLocatorPath() ) );
    QVERIFY( !QFileInfo::exists( store.mutationLockPath() ) );
}

void StorageLocatorTest::oldPendingWithoutPreimageRollsBackSyntheticLocator()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "legacy" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target" ) ),
                                  store.programLocatorPath(), false };
    const QString legacyConfig = QDir{ source.dataRoot }.filePath( QStringLiteral( "ZzLogg.ini" ) );
    QVERIFY( writeRawLocator( legacyConfig, QByteArrayLiteral( "[legacy]\nvalue=true\n" ) ) );
    const StorageMigrationRequest request{
        QStringLiteral( "old-synthetic-pending" ), source, target, legacyConfig, legacyConfig, {}
    };
    QString error;
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    {
        QSettings settings{ store.userLocatorPath(), QSettings::IniFormat };
        settings.remove( QStringLiteral( "Pending/sourceLocatorExisted" ) );
        settings.sync();
        QCOMPARE( settings.status(), QSettings::NoError );
    }

    const auto oldResolution = store.resolve();
    QVERIFY2( oldResolution.state.has_value(), qPrintable( oldResolution.error ) );
    QVERIFY( oldResolution.state->pending.has_value() );
    QVERIFY( !oldResolution.state->pending->sourceLocatorExisted );
    QVERIFY2( store.rollbackPending( request, &error ), qPrintable( error ) );
    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::discardsUnverifiedRolledBackLegacyLocator()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation legacy{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "legacy" ) ),
                                  store.userLocatorPath(), false };
    const QByteArray locator
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=user\ndataRoot=%1\nverified=false\n"
                          "[LastMigration]\ntransactionId=old-rollback\noutcome=rolledBack\n" )
              .arg( legacy.dataRoot )
              .toUtf8();
    QVERIFY( writeRawLocator( store.userLocatorPath(), locator ) );

    QString error;
    QVERIFY2( store.discardUnverifiedLegacyLocator( legacy, &error ), qPrintable( error ) );

    QVERIFY( !QFileInfo::exists( store.userLocatorPath() ) );
    QCOMPARE( store.resolve().source, StorageResolutionSource::Missing );
}

void StorageLocatorTest::writeActiveRejectsReplacingDifferentActiveLocation()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation first{ StorageMode::UserDirectory,
                                 temporaryDirectory.filePath( QStringLiteral( "first" ) ),
                                 store.userLocatorPath(), false };
    const StorageLocation staleReplacement{ StorageMode::ProgramDirectory,
                                            temporaryDirectory.filePath(
                                                QStringLiteral( "replacement" ) ),
                                            store.programLocatorPath(), false };
    QString error;
    QVERIFY2( store.writeActive( first, &error ), qPrintable( error ) );

    QVERIFY( !store.writeActive( staleReplacement, &error ) );

    QVERIFY( error.contains( QStringLiteral( "active" ), Qt::CaseInsensitive ) );
    const auto resolution = store.resolve();
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( first.dataRoot ) );
    QVERIFY( !QFileInfo::exists( store.programLocatorPath() ) );
}

void StorageLocatorTest::commitPendingActivatesUnverifiedTargetAndRemovesSource()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "commit-transaction" ), source, target, {}, {}, {}
    };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );

    QVERIFY2( store.commitPending( request, &error ), qPrintable( error ) );
    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( target.dataRoot ) );
    QVERIFY( !resolution.state->verified );
    QVERIFY( !resolution.state->pending.has_value() );
    QVERIFY( !QFile::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::commitPendingRestoresTargetWhenSourceDeletionFails()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "commit-rollback-transaction" ), source, target, {}, {}, {}
    };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    QVERIFY(
        writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                      temporaryDirectory.filePath( QStringLiteral( "previous-target-root" ) ) ) );
    const QByteArray previousTarget = readBytes( store.programLocatorPath() );
#ifdef Q_OS_WIN
    QFile sourceLock{ store.userLocatorPath() };
    QVERIFY( sourceLock.open( QIODevice::ReadOnly ) );
#else
    const QString sourceDirectory = QFileInfo{ store.userLocatorPath() }.absolutePath();
    QVERIFY(
        QFile::setPermissions( sourceDirectory, QFileDevice::ReadOwner | QFileDevice::ExeOwner ) );
#endif
    const bool committed = store.commitPending( request, &error );
#ifndef Q_OS_WIN
    QVERIFY( QFile::setPermissions( sourceDirectory, QFileDevice::ReadOwner
                                                         | QFileDevice::WriteOwner
                                                         | QFileDevice::ExeOwner ) );
#endif

    QVERIFY( !committed );

    QVERIFY( error.contains( QStringLiteral( "failed to remove source storage locator" ) ) );
    QCOMPARE( readBytes( store.programLocatorPath() ), previousTarget );
    QVERIFY( QFile::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::commitPendingRejectsMismatchedRequest()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "mismatch-commit" ), source, target, {}, {}, {}
    };
    const StorageMigrationRequest mismatched{ request.transactionId,
                                              source,
                                              { StorageMode::ProgramDirectory,
                                                temporaryDirectory.filePath(
                                                    QStringLiteral( "other-target-root" ) ),
                                                store.programLocatorPath(), false },
                                              {},
                                              {},
                                              {} };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );

    QVERIFY( !store.commitPending( mismatched, &error ) );

    QVERIFY( !error.isEmpty() );
    QVERIFY( QFile::exists( store.userLocatorPath() ) );
    QVERIFY( !QFile::exists( store.programLocatorPath() ) );
}

void StorageLocatorTest::rollbackPendingRestoresSourceAndKeepsTargetLocator()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "rollback-transaction" ), source, target, {}, {}, {}
    };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    QVERIFY(
        writeLocator( store.programLocatorPath(), QStringLiteral( "program" ), target.dataRoot ) );

    QVERIFY2( store.rollbackPending( request, &error ), qPrintable( error ) );
    QVERIFY( QFile::exists( store.programLocatorPath() ) );
    QSettings sourceLocator{ store.userLocatorPath(), QSettings::IniFormat };
    QCOMPARE( sourceLocator.value( QStringLiteral( "Storage/dataRoot" ) ).toString(),
              normalized( source.dataRoot ) );
    QVERIFY( !sourceLocator.contains( QStringLiteral( "Pending/transactionId" ) ) );
}

void StorageLocatorTest::rollbackPendingRejectsMismatchedRequest()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "mismatch-rollback" ), source, target, {}, {}, {}
    };
    const StorageMigrationRequest mismatched{ request.transactionId,
                                              { StorageMode::UserDirectory,
                                                temporaryDirectory.filePath(
                                                    QStringLiteral( "other-source-root" ) ),
                                                store.userLocatorPath(), false },
                                              target,
                                              {},
                                              {},
                                              {} };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );

    QVERIFY( !store.rollbackPending( mismatched, &error ) );

    QVERIFY( !error.isEmpty() );
    const auto resolution = store.resolve();
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->pending.has_value() );
    QCOMPARE( resolution.state->pending->source.dataRoot, normalized( source.dataRoot ) );
}

void StorageLocatorTest::writePendingRejectsCommandLineLocation()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageMigrationRequest request{
        QStringLiteral( "command-line-pending" ),
        { StorageMode::CustomDirectory,
          temporaryDirectory.filePath( QStringLiteral( "source-root" ) ), store.userLocatorPath(),
          true },
        { StorageMode::ProgramDirectory,
          temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
          store.programLocatorPath(), false },
        {},
        {},
        {}
    };

    QString error;
    QVERIFY( !store.writePending( request, &error ) );

    QVERIFY( !error.isEmpty() );
    QVERIFY( !QFile::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::writePendingRejectsUserLocationAtProgramLocator()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageMigrationRequest request{
        QStringLiteral( "wrong-locator" ),
        { StorageMode::UserDirectory,
          temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
          store.programLocatorPath(), false },
        { StorageMode::ProgramDirectory,
          temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
          store.programLocatorPath(), false },
        {},
        {},
        {}
    };

    QString error;
    QVERIFY( !store.writePending( request, &error ) );

    QVERIFY( error.contains( QStringLiteral( "locator" ) ) );
    QVERIFY( !QFile::exists( store.programLocatorPath() ) );
    QVERIFY( !QFile::exists( store.userLocatorPath() ) );
}

void StorageLocatorTest::rejectsIncompletePendingGroup()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QByteArray contents
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=program\ndataRoot=data\nverified=true\n"
                          "[Pending]\nsourceMode=program\n" )
              .toUtf8();
    QVERIFY( writeRawLocator( store.programLocatorPath(), contents ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( !resolution.state.has_value() );
    QVERIFY( resolution.error.contains( QStringLiteral( "Pending" ) ) );
}

void StorageLocatorTest::rejectsPendingWhoseSourceDiffersFromActive()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString targetRoot = temporaryDirectory.filePath( QStringLiteral( "target-root" ) );
    const QByteArray contents
        = QStringLiteral(
              "[Storage]\nformatVersion=1\nmode=program\ndataRoot=data\nverified=true\n"
              "[Pending]\ntransactionId=pending-id\nsourceMode=program\nsourceRoot=%"
              "1\nsourceLocator=%2\n"
              "targetMode=custom\ntargetRoot=%3\ntargetLocator=%4\nlegacyConfigFile=config.ini\n"
              "legacySessionFile=session.ini\nsourceLogsDirectory=logs\n" )
              .arg( temporaryDirectory.filePath( QStringLiteral( "other-source" ) ),
                    store.programLocatorPath(), targetRoot, store.userLocatorPath() )
              .toUtf8();
    QVERIFY( writeRawLocator( store.programLocatorPath(), contents ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( !resolution.state.has_value() );
    QVERIFY( resolution.error.contains( QStringLiteral( "source" ) ) );
}

void StorageLocatorTest::rejectsIncompleteLastMigrationGroup()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QByteArray contents
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=program\ndataRoot=data\nverified=true\n"
                          "[LastMigration]\ntransactionId=missing-outcome\n" )
              .toUtf8();
    QVERIFY( writeRawLocator( store.programLocatorPath(), contents ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( !resolution.state.has_value() );
    QVERIFY( resolution.error.contains( QStringLiteral( "LastMigration" ) ) );
}

void StorageLocatorTest::rejectsPendingTogetherWithLastMigration()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::UserDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
                                  store.userLocatorPath(), false };
    const StorageLocation target{ StorageMode::ProgramDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.programLocatorPath(), false };
    const StorageMigrationRequest request{
        QStringLiteral( "ambiguous-transaction" ), source, target, {}, {}, {}
    };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    {
        QSettings settings{ store.userLocatorPath(), QSettings::IniFormat };
        settings.setValue( QStringLiteral( "LastMigration/transactionId" ), request.transactionId );
        settings.setValue( QStringLiteral( "LastMigration/outcome" ),
                           QStringLiteral( "committed" ) );
        settings.sync();
        QCOMPARE( settings.status(), QSettings::NoError );
    }

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::UserLocator );
    QVERIFY( !resolution.state.has_value() );
    QVERIFY( resolution.error.contains( QStringLiteral( "Pending" ) ) );
    QVERIFY( resolution.error.contains( QStringLiteral( "LastMigration" ) ) );
}

void StorageLocatorTest::roundTripsCompleteProgramRelativePending()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const StorageLocation source{ StorageMode::ProgramDirectory, QStringLiteral( "data" ),
                                  store.programLocatorPath(), false };
    const StorageLocation target{ StorageMode::CustomDirectory,
                                  temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
                                  store.userLocatorPath(), false };
    StorageMigrationRequest request{
        QStringLiteral( "complete-pending" ),
        source,
        target,
        temporaryDirectory.filePath( QStringLiteral( "legacy-config.ini" ) ),
        temporaryDirectory.filePath( QStringLiteral( "legacy-session.ini" ) ),
        temporaryDirectory.filePath( QStringLiteral( "legacy-logs" ) )
    };
    request.sourceLogsDirectory = temporaryDirectory.filePath( QStringLiteral( "stored-logs" ) );
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );

    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->verified );
    QVERIFY( resolution.state->pending.has_value() );
    const StorageMigrationRequest& pending = *resolution.state->pending;
    QCOMPARE( pending.transactionId, request.transactionId );
    QCOMPARE( pending.source.mode, StorageMode::ProgramDirectory );
    QCOMPARE( pending.source.dataRoot,
              normalized( temporaryDirectory.filePath( QStringLiteral( "app/data" ) ) ) );
    QCOMPARE( pending.source.locatorPath, store.programLocatorPath() );
    QCOMPARE( pending.target.mode, StorageMode::CustomDirectory );
    QCOMPARE( pending.target.dataRoot, normalized( target.dataRoot ) );
    QCOMPARE( pending.target.locatorPath, store.userLocatorPath() );
    QCOMPARE( pending.legacyConfigFile, request.legacyConfigFile );
    QCOMPARE( pending.legacySessionFile, request.legacySessionFile );
    QCOMPARE( pending.sourceLogsDirectory, request.sourceLogsDirectory );
    QCOMPARE( pending.source.mode, resolution.state->active.mode );
    QCOMPARE( pending.source.dataRoot, resolution.state->active.dataRoot );
    QCOMPARE( pending.source.locatorPath, resolution.state->active.locatorPath );
}

void StorageLocatorTest::readsPendingWithoutSourceLogsAsLegacyCompatible()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString sourceRoot = temporaryDirectory.filePath( QStringLiteral( "source" ) );
    const QString targetRoot = temporaryDirectory.filePath( QStringLiteral( "target" ) );
    const QByteArray locator
        = QStringLiteral( "[Storage]\nformatVersion=1\nmode=user\ndataRoot=%1\nverified=true\n"
                          "[Pending]\ntransactionId=old-pending\nsourceMode=user\nsourceRoot=%1\n"
                          "sourceLocator=%2\ntargetMode=program\ntargetRoot=%3\ntargetLocator=%4\n"
                          "legacyConfigFile=\nlegacySessionFile=\n" )
              .arg( sourceRoot, store.userLocatorPath(), targetRoot, store.programLocatorPath() )
              .toUtf8();
    QVERIFY( writeRawLocator( store.userLocatorPath(), locator ) );

    const auto resolution = store.resolve();

    QVERIFY2( resolution.state.has_value(), qPrintable( resolution.error ) );
    QVERIFY( resolution.state->pending.has_value() );
    QVERIFY( resolution.state->pending->sourceLogsDirectory.isEmpty() );
}

QTEST_MAIN( StorageLocatorTest )
#include "storagelocatortest.moc"
