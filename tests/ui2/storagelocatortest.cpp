#include "storagelocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    const QByteArray contents = QStringLiteral( "[Storage]\nformatVersion=1\nmode=%1\ndataRoot=%2\nverified=true\n%3" )
                                    .arg( mode, dataRoot, extraStorageFields )
                                    .toUtf8();
    return locator.write( contents ) == contents.size() && locator.commit();
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
    void writeActiveReplacesConflictingLocatorOnlyAfterTargetWrite();
    void writeActiveRestoresTargetWhenConflictingLocatorCannotBeDeleted();
    void writePendingPreservesVerifiedSourceState();
    void commitPendingActivatesUnverifiedTargetAndRemovesSource();
    void commitPendingRestoresTargetWhenSourceDeletionFails();
    void commitPendingRejectsMismatchedRequest();
    void rollbackPendingRestoresSourceAndKeepsTargetLocator();
    void rollbackPendingRejectsMismatchedRequest();
    void writePendingRejectsCommandLineLocation();
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
    QCOMPARE( store.userLocatorPath(), normalized( configDirectory + QStringLiteral( "/storage.ini" ) ) );
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
    QCOMPARE( resolution.state->active.dataRoot, normalized( applicationDirectory + QStringLiteral( "/data" ) ) );
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

void StorageLocatorTest::writeActiveReplacesConflictingLocatorOnlyAfterTargetWrite()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString userRoot = temporaryDirectory.filePath( QStringLiteral( "user-root" ) );
    const QString programRoot = temporaryDirectory.filePath( QStringLiteral( "program-root" ) );
    QVERIFY( writeLocator( store.userLocatorPath(), QStringLiteral( "user" ), userRoot ) );

    QString error;
    QVERIFY2( store.writeActive( { StorageMode::ProgramDirectory, programRoot, {}, false }, &error ),
              qPrintable( error ) );

    QVERIFY( QFile::exists( store.programLocatorPath() ) );
    QVERIFY( !QFile::exists( store.userLocatorPath() ) );
    const auto resolution = store.resolve();
    QCOMPARE( resolution.source, StorageResolutionSource::ProgramLocator );
    QVERIFY( resolution.state.has_value() );
    QCOMPARE( resolution.state->active.dataRoot, normalized( programRoot ) );
}

void StorageLocatorTest::writeActiveRestoresTargetWhenConflictingLocatorCannotBeDeleted()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const StorageLocatorStore store{ temporaryDirectory.filePath( QStringLiteral( "app" ) ),
                                     temporaryDirectory.filePath( QStringLiteral( "config" ) ) };
    const QString previousProgramRoot = temporaryDirectory.filePath( QStringLiteral( "previous-program-root" ) );
    const QString replacementProgramRoot = temporaryDirectory.filePath( QStringLiteral( "replacement-program-root" ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ), previousProgramRoot ) );
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
    const StorageMigrationRequest request{ QStringLiteral( "pending-transaction" ), source, target,
                                           QStringLiteral( "legacy.ini" ), QStringLiteral( "session.ini" ),
                                           QStringLiteral( "crashes" ) };

    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    const auto resolution = store.resolve();

    QCOMPARE( resolution.source, StorageResolutionSource::UserLocator );
    QVERIFY( resolution.state.has_value() );
    QVERIFY( resolution.state->verified );
    QCOMPARE( resolution.state->active.dataRoot, normalized( source.dataRoot ) );
    QVERIFY( resolution.state->pending.has_value() );
    QCOMPARE( resolution.state->pending->transactionId, request.transactionId );
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
    const StorageMigrationRequest request{ QStringLiteral( "commit-transaction" ), source, target, {}, {}, {} };
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
    const StorageMigrationRequest request{ QStringLiteral( "commit-rollback-transaction" ), source,
                                           target, {}, {}, {} };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ),
                           temporaryDirectory.filePath( QStringLiteral( "previous-target-root" ) ) ) );
    const QByteArray previousTarget = readBytes( store.programLocatorPath() );
#ifdef Q_OS_WIN
    QFile sourceLock{ store.userLocatorPath() };
    QVERIFY( sourceLock.open( QIODevice::ReadOnly ) );
#else
    const QString sourceDirectory = QFileInfo{ store.userLocatorPath() }.absolutePath();
    QVERIFY( QFile::setPermissions( sourceDirectory,
                                    QFileDevice::ReadOwner | QFileDevice::ExeOwner ) );
#endif
    const bool committed = store.commitPending( request, &error );
#ifndef Q_OS_WIN
    QVERIFY( QFile::setPermissions( sourceDirectory,
                                    QFileDevice::ReadOwner | QFileDevice::WriteOwner
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
    const StorageMigrationRequest request{ QStringLiteral( "mismatch-commit" ), source, target, {}, {}, {} };
    const StorageMigrationRequest mismatched{ request.transactionId, source,
                                              { StorageMode::ProgramDirectory,
                                                temporaryDirectory.filePath( QStringLiteral( "other-target-root" ) ),
                                                store.programLocatorPath(), false },
                                              {}, {}, {} };
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
    const StorageMigrationRequest request{ QStringLiteral( "rollback-transaction" ), source, target, {}, {}, {} };
    QString error;
    QVERIFY2( store.writeActive( source, &error ), qPrintable( error ) );
    QVERIFY2( store.writePending( request, &error ), qPrintable( error ) );
    QVERIFY( writeLocator( store.programLocatorPath(), QStringLiteral( "program" ), target.dataRoot ) );

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
    const StorageMigrationRequest request{ QStringLiteral( "mismatch-rollback" ), source, target, {}, {}, {} };
    const StorageMigrationRequest mismatched{ request.transactionId,
                                              { StorageMode::UserDirectory,
                                                temporaryDirectory.filePath( QStringLiteral( "other-source-root" ) ),
                                                store.userLocatorPath(), false },
                                              target, {}, {}, {} };
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
        { StorageMode::CustomDirectory, temporaryDirectory.filePath( QStringLiteral( "source-root" ) ),
          store.userLocatorPath(), true },
        { StorageMode::ProgramDirectory, temporaryDirectory.filePath( QStringLiteral( "target-root" ) ),
          store.programLocatorPath(), false },
        {}, {}, {} };

    QString error;
    QVERIFY( !store.writePending( request, &error ) );

    QVERIFY( !error.isEmpty() );
    QVERIFY( !QFile::exists( store.userLocatorPath() ) );
}

QTEST_MAIN( StorageLocatorTest )
#include "storagelocatortest.moc"
