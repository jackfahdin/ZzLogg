#include "storagelocator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>

#include <memory>
#include <utility>

namespace {

struct FileSnapshot {
    bool existed = false;
    bool readable = true;
    QByteArray bytes;
};

struct LastMigrationRecord {
    QString transactionId;
    QString outcome;
};

QString normalizedPath( const QString& path )
{
    return path.isEmpty() ? QString{} : QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool samePath( const QString& left, const QString& right )
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    return normalizedPath( left ).compare( normalizedPath( right ), caseSensitivity ) == 0;
}

bool isAbsolutePath( const QString& path )
{
    return !path.isEmpty() && QFileInfo{ path }.isAbsolute();
}

void setError( QString* error, const QString& message )
{
    if ( error != nullptr ) {
        *error = message;
    }
}

QString modeName( StorageMode mode )
{
    switch ( mode ) {
    case StorageMode::UserDirectory:
        return QStringLiteral( "user" );
    case StorageMode::ProgramDirectory:
        return QStringLiteral( "program" );
    case StorageMode::CustomDirectory:
        return QStringLiteral( "custom" );
    }
    return {};
}

std::optional<StorageMode> storageMode( const QString& value )
{
    if ( value == QStringLiteral( "user" ) ) {
        return StorageMode::UserDirectory;
    }
    if ( value == QStringLiteral( "program" ) ) {
        return StorageMode::ProgramDirectory;
    }
    if ( value == QStringLiteral( "custom" ) ) {
        return StorageMode::CustomDirectory;
    }
    return std::nullopt;
}

bool boolValue( const QSettings& settings, const QString& key, bool* value )
{
    if ( !settings.contains( key ) ) {
        return false;
    }
    const QString serialized = settings.value( key ).toString();
    if ( serialized == QStringLiteral( "true" ) ) {
        *value = true;
        return true;
    }
    if ( serialized == QStringLiteral( "false" ) ) {
        *value = false;
        return true;
    }
    return false;
}

FileSnapshot snapshot( const QString& path )
{
    QFile file{ path };
    if ( !file.exists() ) {
        return {};
    }
    if ( !QFileInfo{ path }.isFile() || !file.open( QIODevice::ReadOnly ) ) {
        return { true, false, {} };
    }
    return { true, true, file.readAll() };
}

bool sameLocation( const StorageLocation& left, const StorageLocation& right )
{
    return left.mode == right.mode && samePath( left.dataRoot, right.dataRoot )
           && samePath( left.locatorPath, right.locatorPath )
           && left.commandLineOverride == right.commandLineOverride;
}

bool sameRequest( const StorageMigrationRequest& left, const StorageMigrationRequest& right )
{
    return left.transactionId == right.transactionId && sameLocation( left.source, right.source )
           && sameLocation( left.target, right.target )
           && samePath( left.legacyConfigFile, right.legacyConfigFile )
           && samePath( left.legacySessionFile, right.legacySessionFile )
           && samePath( left.legacyCrashDirectory, right.legacyCrashDirectory )
           && samePath( left.sourceLogsDirectory, right.sourceLogsDirectory );
}

struct LocatorPaths {
    QString applicationDirectory;
    QString program;
    QString user;
};

QString locatorMutationLockPath( const LocatorPaths& paths )
{
    QString program = normalizedPath( paths.program );
    QString user = normalizedPath( paths.user );
#ifdef Q_OS_WIN
    program = program.toCaseFolded();
    user = user.toCaseFolded();
#endif
    QByteArray identity = program.toUtf8();
    identity.append( '\0' );
    identity.append( user.toUtf8() );
    const QByteArray digest
        = QCryptographicHash::hash( identity, QCryptographicHash::Sha256 ).toHex();
    return QDir{ QDir::tempPath() }.filePath(
        QStringLiteral( ".zzlogg-storage-%1.lock" ).arg( QString::fromLatin1( digest ) ) );
}

std::unique_ptr<QLockFile> lockLocatorMutations( const LocatorPaths& paths, QString* error )
{
    const QString lockPath = locatorMutationLockPath( paths );
    const QString parent = QFileInfo{ lockPath }.absolutePath();
    if ( !QDir{}.mkpath( parent ) ) {
        setError(
            error,
            QStringLiteral( "failed to create storage locator lock directory: %1" ).arg( parent ) );
        return {};
    }
    auto lock = std::make_unique<QLockFile>( lockPath );
    lock->setStaleLockTime( 30000 );
    if ( !lock->tryLock( 0 ) ) {
        setError(
            error,
            QStringLiteral( "storage locator mutation lock is already held: %1" ).arg( lockPath ) );
        return {};
    }
    return lock;
}

bool oldPendingSourceLocatorExisted( bool verified, const StorageLocation& source,
                                     const QString& legacyConfigFile,
                                     const QString& legacySessionFile,
                                     const QString& legacyCrashDirectory,
                                     const QString& sourceLogsDirectory )
{
    const bool hasLegacyInputs = !legacyConfigFile.isEmpty() || !legacySessionFile.isEmpty()
                                 || !legacyCrashDirectory.isEmpty()
                                 || !sourceLogsDirectory.isEmpty();
    const QString manifest
        = QDir{ source.dataRoot }.filePath( QStringLiteral( "storage-manifest.ini" ) );
    return verified || !hasLegacyInputs || QFileInfo{ manifest }.isFile();
}

struct ReadResult {
    bool found = false;
    std::optional<StorageLocatorState> state;
    QString error;
};

QString pathForActiveLocation( const StorageLocation& location, const LocatorPaths& paths )
{
    return location.mode == StorageMode::ProgramDirectory ? paths.program : paths.user;
}

bool normalizedLocatorPath( const QString& requested, const StorageLocation& location,
                            const LocatorPaths& paths, QString* path, QString* error )
{
    const QString expected = pathForActiveLocation( location, paths );
    const QString result = requested.isEmpty() ? expected : normalizedPath( requested );
    if ( !samePath( result, expected ) ) {
        setError( error,
                  location.mode == StorageMode::ProgramDirectory
                      ? QStringLiteral( "program storage must use the adjacent program locator" )
                      : QStringLiteral( "user and custom storage must use the user locator" ) );
        return false;
    }
    *path = expected;
    return true;
}

bool normalizedLocation( const StorageLocation& input, const QString& locatorPath,
                         const LocatorPaths& paths, StorageLocation* output, QString* error )
{
    const QString rawRoot = normalizedPath( input.dataRoot );
    QString root;
    if ( rawRoot == QStringLiteral( "data" ) ) {
        if ( input.mode != StorageMode::ProgramDirectory
             || !samePath( locatorPath, paths.program ) ) {
            setError( error, QStringLiteral(
                                 "relative storage paths are only allowed for program data" ) );
            return false;
        }
        root = QDir{ paths.applicationDirectory }.filePath( QStringLiteral( "data" ) );
    }
    else if ( isAbsolutePath( rawRoot ) ) {
        root = rawRoot;
    }
    else {
        setError( error, QStringLiteral( "storage data root must be an absolute path" ) );
        return false;
    }
    *output = { input.mode, normalizedPath( root ), locatorPath, input.commandLineOverride };
    return true;
}

bool normalizedLegacyPath( const QString& input, const QString& label, QString* output,
                           QString* error )
{
    if ( input.isEmpty() ) {
        output->clear();
        return true;
    }
    const QString path = normalizedPath( input );
    if ( !isAbsolutePath( path ) ) {
        setError( error, QStringLiteral( "%1 must be an absolute path: %2" ).arg( label, input ) );
        return false;
    }
    *output = path;
    return true;
}

QString storedRoot( const StorageLocation& location, const LocatorPaths& paths )
{
    const QString programData
        = QDir{ paths.applicationDirectory }.filePath( QStringLiteral( "data" ) );
    return location.mode == StorageMode::ProgramDirectory
                   && samePath( location.locatorPath, paths.program )
                   && samePath( location.dataRoot, programData )
               ? QStringLiteral( "data" )
               : location.dataRoot;
}

bool writeState( const QString& path, const StorageLocatorState& state, const LocatorPaths& paths,
                 QString* error,
                 const std::optional<LastMigrationRecord>& lastMigration = std::nullopt )
{
    const QString parent = QFileInfo{ path }.absolutePath();
    if ( !QDir{}.mkpath( parent ) ) {
        setError( error, QStringLiteral( "failed to create locator directory: %1" ).arg( parent ) );
        return false;
    }
    const QString stagingPath = QDir{ parent }.filePath(
        QStringLiteral( ".zzlogg-locator-state-%1.ini" )
            .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) ) );
    {
        QSettings settings{ stagingPath, QSettings::IniFormat };
        settings.clear();
        settings.setValue( QStringLiteral( "Storage/formatVersion" ), state.formatVersion );
        settings.setValue( QStringLiteral( "Storage/mode" ), modeName( state.active.mode ) );
        settings.setValue( QStringLiteral( "Storage/dataRoot" ),
                           storedRoot( state.active, paths ) );
        settings.setValue( QStringLiteral( "Storage/verified" ), state.verified );
        if ( state.pending.has_value() ) {
            const StorageMigrationRequest& pending = *state.pending;
            settings.setValue( QStringLiteral( "Pending/transactionId" ), pending.transactionId );
            settings.setValue( QStringLiteral( "Pending/sourceMode" ),
                               modeName( pending.source.mode ) );
            settings.setValue( QStringLiteral( "Pending/sourceRoot" ),
                               storedRoot( pending.source, paths ) );
            settings.setValue( QStringLiteral( "Pending/sourceLocator" ),
                               pending.source.locatorPath );
            settings.setValue( QStringLiteral( "Pending/targetMode" ),
                               modeName( pending.target.mode ) );
            settings.setValue( QStringLiteral( "Pending/targetRoot" ),
                               storedRoot( pending.target, paths ) );
            settings.setValue( QStringLiteral( "Pending/targetLocator" ),
                               pending.target.locatorPath );
            settings.setValue( QStringLiteral( "Pending/legacyConfigFile" ),
                               pending.legacyConfigFile );
            settings.setValue( QStringLiteral( "Pending/legacySessionFile" ),
                               pending.legacySessionFile );
            settings.setValue( QStringLiteral( "Pending/legacyCrashDirectory" ),
                               pending.legacyCrashDirectory );
            settings.setValue( QStringLiteral( "Pending/sourceLogsDirectory" ),
                               pending.sourceLogsDirectory );
            settings.setValue( QStringLiteral( "Pending/sourceLocatorExisted" ),
                               pending.sourceLocatorExisted );
        }
        if ( lastMigration.has_value() ) {
            settings.setValue( QStringLiteral( "LastMigration/transactionId" ),
                               lastMigration->transactionId );
            settings.setValue( QStringLiteral( "LastMigration/outcome" ), lastMigration->outcome );
        }
        settings.sync();
        if ( settings.status() != QSettings::NoError ) {
            QFile::remove( stagingPath );
            setError( error, QStringLiteral( "failed to serialize locator: %1" ).arg( path ) );
            return false;
        }
    }
    QFile contents{ stagingPath };
    if ( !contents.open( QIODevice::ReadOnly ) ) {
        QFile::remove( stagingPath );
        setError( error, QStringLiteral( "failed to read serialized locator: %1" ).arg( path ) );
        return false;
    }
    const QByteArray bytes = contents.readAll();
    contents.close();
    QFile::remove( stagingPath );
    QSaveFile locator{ path };
    if ( !locator.open( QIODevice::WriteOnly ) || locator.write( bytes ) != bytes.size()
         || !locator.commit() ) {
        setError( error, QStringLiteral( "failed to atomically write locator: %1" ).arg( path ) );
        return false;
    }
    return true;
}

bool parseLocation( const QSettings& settings, const QString& modeKey, const QString& rootKey,
                    const QString& locatorPath, const LocatorPaths& paths, StorageLocation* output,
                    QString* error )
{
    const auto mode = storageMode( settings.value( modeKey ).toString() );
    if ( !mode.has_value() || !settings.contains( rootKey ) ) {
        setError( error, QStringLiteral( "storage locator has an invalid mode or data root" ) );
        return false;
    }
    StorageLocation input{ *mode, settings.value( rootKey ).toString(), locatorPath, false };
    return normalizedLocation( input, locatorPath, paths, output, error );
}

ReadResult readState( const QString& path, const LocatorPaths& paths )
{
    QFileInfo fileInfo{ path };
    if ( !fileInfo.exists() ) {
        return {};
    }
    if ( !fileInfo.isFile() ) {
        return { true, std::nullopt,
                 QStringLiteral( "storage locator is not a file: %1" ).arg( path ) };
    }

    QSettings settings{ path, QSettings::IniFormat };
    bool versionOk = false;
    const int formatVersion
        = settings.value( QStringLiteral( "Storage/formatVersion" ) ).toInt( &versionOk );
    if ( !versionOk || formatVersion != 1 ) {
        return {
            true, std::nullopt,
            QStringLiteral( "storage locator has an unsupported format version: %1" ).arg( path )
        };
    }
    StorageLocation active;
    QString parseError;
    if ( !parseLocation( settings, QStringLiteral( "Storage/mode" ),
                         QStringLiteral( "Storage/dataRoot" ), path, paths, &active,
                         &parseError ) ) {
        return { true, std::nullopt, parseError + QStringLiteral( ": %1" ).arg( path ) };
    }
    bool verified = false;
    if ( !boolValue( settings, QStringLiteral( "Storage/verified" ), &verified ) ) {
        return {
            true, std::nullopt,
            QStringLiteral( "storage locator has an invalid verified value: %1" ).arg( path )
        };
    }

    StorageLocatorState state{ formatVersion, active, std::nullopt, verified };
    const QStringList groups = settings.childGroups();
    if ( groups.contains( QStringLiteral( "Pending" ) )
         && groups.contains( QStringLiteral( "LastMigration" ) ) ) {
        return { true, std::nullopt,
                 QStringLiteral(
                     "storage locator cannot contain both Pending and LastMigration: %1" )
                     .arg( path ) };
    }
    if ( groups.contains( QStringLiteral( "Pending" ) ) ) {
        const QStringList pendingKeys{ QStringLiteral( "Pending/transactionId" ),
                                       QStringLiteral( "Pending/sourceMode" ),
                                       QStringLiteral( "Pending/sourceRoot" ),
                                       QStringLiteral( "Pending/sourceLocator" ),
                                       QStringLiteral( "Pending/targetMode" ),
                                       QStringLiteral( "Pending/targetRoot" ),
                                       QStringLiteral( "Pending/targetLocator" ),
                                       QStringLiteral( "Pending/legacyConfigFile" ),
                                       QStringLiteral( "Pending/legacySessionFile" ),
                                       QStringLiteral( "Pending/legacyCrashDirectory" ) };
        for ( const QString& key : pendingKeys ) {
            if ( !settings.contains( key ) ) {
                return {
                    true, std::nullopt,
                    QStringLiteral( "storage locator Pending is missing %1: %2" ).arg( key, path )
                };
            }
        }
        const QString transactionId
            = settings.value( QStringLiteral( "Pending/transactionId" ) ).toString();
        const QString serializedSourceLocator = normalizedPath(
            settings.value( QStringLiteral( "Pending/sourceLocator" ) ).toString() );
        const QString serializedTargetLocator = normalizedPath(
            settings.value( QStringLiteral( "Pending/targetLocator" ) ).toString() );
        if ( transactionId.isEmpty()
             || ( !samePath( serializedSourceLocator, paths.program )
                  && !samePath( serializedSourceLocator, paths.user ) )
             || ( !samePath( serializedTargetLocator, paths.program )
                  && !samePath( serializedTargetLocator, paths.user ) )
             || !samePath( serializedSourceLocator, path ) ) {
            return { true, std::nullopt,
                     QStringLiteral( "storage locator has invalid pending migration metadata: %1" )
                         .arg( path ) };
        }
        const QString sourceLocator
            = samePath( serializedSourceLocator, paths.program ) ? paths.program : paths.user;
        const QString targetLocator
            = samePath( serializedTargetLocator, paths.program ) ? paths.program : paths.user;
        StorageLocation source;
        StorageLocation target;
        if ( !parseLocation( settings, QStringLiteral( "Pending/sourceMode" ),
                             QStringLiteral( "Pending/sourceRoot" ), sourceLocator, paths, &source,
                             &parseError )
             || !parseLocation( settings, QStringLiteral( "Pending/targetMode" ),
                                QStringLiteral( "Pending/targetRoot" ), targetLocator, paths,
                                &target, &parseError ) ) {
            return { true, std::nullopt, parseError + QStringLiteral( ": %1" ).arg( path ) };
        }
        if ( !sameLocation( source, active ) ) {
            return { true, std::nullopt,
                     QStringLiteral(
                         "storage locator Pending source does not match active storage: %1" )
                         .arg( path ) };
        }
        const QString legacyConfigFile
            = settings.value( QStringLiteral( "Pending/legacyConfigFile" ) ).toString();
        const QString legacySessionFile
            = settings.value( QStringLiteral( "Pending/legacySessionFile" ) ).toString();
        const QString legacyCrashDirectory
            = settings.value( QStringLiteral( "Pending/legacyCrashDirectory" ) ).toString();
        const QString sourceLogsDirectory
            = settings.value( QStringLiteral( "Pending/sourceLogsDirectory" ) ).toString();
        bool sourceLocatorExisted
            = oldPendingSourceLocatorExisted( verified, source, legacyConfigFile, legacySessionFile,
                                              legacyCrashDirectory, sourceLogsDirectory );
        if ( settings.contains( QStringLiteral( "Pending/sourceLocatorExisted" ) )
             && !boolValue( settings, QStringLiteral( "Pending/sourceLocatorExisted" ),
                            &sourceLocatorExisted ) ) {
            return { true, std::nullopt,
                     QStringLiteral( "storage locator Pending has invalid source preimage: %1" )
                         .arg( path ) };
        }
        state.pending = StorageMigrationRequest{ transactionId,
                                                 source,
                                                 target,
                                                 legacyConfigFile,
                                                 legacySessionFile,
                                                 legacyCrashDirectory,
                                                 sourceLogsDirectory,
                                                 sourceLocatorExisted };
    }
    if ( groups.contains( QStringLiteral( "LastMigration" ) ) ) {
        const QString transactionId
            = settings.value( QStringLiteral( "LastMigration/transactionId" ) ).toString();
        const QString outcome
            = settings.value( QStringLiteral( "LastMigration/outcome" ) ).toString();
        if ( transactionId.isEmpty()
             || ( outcome != QStringLiteral( "committed" )
                  && outcome != QStringLiteral( "rolledBack" ) ) ) {
            return { true, std::nullopt,
                     QStringLiteral( "storage locator has invalid LastMigration metadata: %1" )
                         .arg( path ) };
        }
    }
    if ( settings.status() != QSettings::NoError ) {
        return { true, std::nullopt,
                 QStringLiteral( "failed to read storage locator: %1" ).arg( path ) };
    }
    return { true, state, {} };
}

bool normalizedRequest( const StorageMigrationRequest& input, const LocatorPaths& paths,
                        StorageMigrationRequest* output, QString* error )
{
    if ( input.transactionId.isEmpty() ) {
        setError( error, QStringLiteral( "storage migration transaction id is empty" ) );
        return false;
    }
    if ( input.source.commandLineOverride || input.target.commandLineOverride ) {
        setError( error, QStringLiteral( "command-line storage locations cannot be persisted" ) );
        return false;
    }
    QString sourcePath;
    QString targetPath;
    if ( !normalizedLocatorPath( input.source.locatorPath, input.source, paths, &sourcePath, error )
         || !normalizedLocatorPath( input.target.locatorPath, input.target, paths, &targetPath,
                                    error ) ) {
        return false;
    }
    StorageLocation source;
    StorageLocation target;
    if ( !normalizedLocation( input.source, sourcePath, paths, &source, error )
         || !normalizedLocation( input.target, targetPath, paths, &target, error ) ) {
        return false;
    }
    QString legacyConfigFile;
    QString legacySessionFile;
    QString legacyCrashDirectory;
    QString sourceLogsDirectory;
    if ( !normalizedLegacyPath( input.legacyConfigFile, QStringLiteral( "legacy config file" ),
                                &legacyConfigFile, error )
         || !normalizedLegacyPath( input.legacySessionFile, QStringLiteral( "legacy session file" ),
                                   &legacySessionFile, error )
         || !normalizedLegacyPath( input.legacyCrashDirectory,
                                   QStringLiteral( "legacy crash directory" ),
                                   &legacyCrashDirectory, error )
         || !normalizedLegacyPath( input.sourceLogsDirectory,
                                   QStringLiteral( "source logs directory" ), &sourceLogsDirectory,
                                   error ) ) {
        return false;
    }
    *output = { input.transactionId,
                source,
                target,
                legacyConfigFile,
                legacySessionFile,
                legacyCrashDirectory,
                sourceLogsDirectory,
                input.sourceLocatorExisted };
    return true;
}

bool restore( const QString& path, const FileSnapshot& previous, QString* error )
{
    if ( !previous.existed ) {
        if ( !QFile::exists( path ) || QFile::remove( path ) ) {
            return true;
        }
        setError( error,
                  QStringLiteral( "failed to remove newly written locator: %1" ).arg( path ) );
        return false;
    }
    QSaveFile file{ path };
    if ( !file.open( QIODevice::WriteOnly ) || file.write( previous.bytes ) != previous.bytes.size()
         || !file.commit() ) {
        setError( error, QStringLiteral( "failed to restore locator: %1" ).arg( path ) );
        return false;
    }
    return true;
}

} // namespace

StorageLocatorStore::StorageLocatorStore( QString applicationDirectory, QString appConfigDirectory )
    : applicationDirectory_( normalizedPath( std::move( applicationDirectory ) ) )
    , appConfigDirectory_( normalizedPath( std::move( appConfigDirectory ) ) )
{
}

QString StorageLocatorStore::programLocatorPath() const
{
    return QDir{ applicationDirectory_ }.filePath( QStringLiteral( "ZzLogg.storage.ini" ) );
}

QString StorageLocatorStore::userLocatorPath() const
{
    return QDir{ appConfigDirectory_ }.filePath( QStringLiteral( "storage.ini" ) );
}

QString StorageLocatorStore::mutationLockPath() const
{
    return locatorMutationLockPath(
        { applicationDirectory_, programLocatorPath(), userLocatorPath() } );
}

StorageResolution StorageLocatorStore::resolve( const QString& commandLineDataRoot ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    if ( !commandLineDataRoot.isEmpty() ) {
        const QString root = normalizedPath( commandLineDataRoot );
        if ( !isAbsolutePath( root ) ) {
            return { StorageResolutionSource::CommandLine, std::nullopt,
                     QStringLiteral( "command-line storage root must be an absolute path" ) };
        }
        StorageLocation active{ StorageMode::CustomDirectory, root, {}, true };
        return { StorageResolutionSource::CommandLine,
                 StorageLocatorState{ 1, active, std::nullopt, false },
                 {} };
    }

    const ReadResult program = readState( paths.program, paths );
    if ( program.found ) {
        return { StorageResolutionSource::ProgramLocator, program.state, program.error };
    }
    const ReadResult user = readState( paths.user, paths );
    if ( user.found ) {
        return { StorageResolutionSource::UserLocator, user.state, user.error };
    }
    return {};
}

bool StorageLocatorStore::writeActive( const StorageLocation& location, QString* error ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    if ( location.commandLineOverride ) {
        setError( error, QStringLiteral( "command-line storage locations cannot be persisted" ) );
        return false;
    }
    QString targetPath;
    if ( !normalizedLocatorPath( location.locatorPath, location, paths, &targetPath, error ) ) {
        return false;
    }
    StorageLocation active;
    if ( !normalizedLocation( location, targetPath, paths, &active, error ) ) {
        return false;
    }
    auto mutationLock = lockLocatorMutations( paths, error );
    if ( !mutationLock ) {
        return false;
    }
    const StorageResolution current = resolve();
    if ( !current.error.isEmpty() ) {
        setError( error, current.error );
        return false;
    }
    if ( current.state.has_value() ) {
        if ( current.state->pending.has_value() ) {
            setError( error, QStringLiteral( "active storage has a pending migration" ) );
            return false;
        }
        if ( !sameLocation( current.state->active, active ) ) {
            setError( error, QStringLiteral( "active storage changed before locator write" ) );
            return false;
        }
    }
    const StorageLocatorState state{ 1, active, std::nullopt, true };
    const FileSnapshot previous = snapshot( targetPath );
    if ( !previous.readable ) {
        setError(
            error,
            QStringLiteral( "failed to read existing storage locator: %1" ).arg( targetPath ) );
        return false;
    }
    if ( !writeState( targetPath, state, paths, error ) ) {
        return false;
    }
    const QString conflictingPath
        = samePath( targetPath, paths.program ) ? paths.user : paths.program;
    if ( QFileInfo{ conflictingPath }.exists() && !QFile::remove( conflictingPath ) ) {
        QString restoreError;
        const bool restored = restore( targetPath, previous, &restoreError );
        setError( error,
                  restored
                      ? QStringLiteral( "failed to remove conflicting storage locator: %1" )
                            .arg( conflictingPath )
                      : QStringLiteral( "failed to remove conflicting storage locator: %1; %2" )
                            .arg( conflictingPath, restoreError ) );
        return false;
    }
    return true;
}

bool StorageLocatorStore::writePending( const StorageMigrationRequest& request,
                                        QString* error ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    StorageMigrationRequest normalized;
    if ( !normalizedRequest( request, paths, &normalized, error ) ) {
        return false;
    }
    auto mutationLock = lockLocatorMutations( paths, error );
    if ( !mutationLock ) {
        return false;
    }
    const ReadResult existing = readState( normalized.source.locatorPath, paths );
    if ( existing.found && !existing.state.has_value() ) {
        setError( error, existing.error );
        return false;
    }
    const StorageResolution current = resolve();
    if ( !current.error.isEmpty() ) {
        setError( error, current.error );
        return false;
    }
    if ( current.state.has_value() && !sameLocation( current.state->active, normalized.source ) ) {
        setError( error, QStringLiteral( "active storage changed before pending migration" ) );
        return false;
    }
    if ( existing.state.has_value() ) {
        if ( !sameLocation( existing.state->active, normalized.source ) ) {
            setError( error,
                      QStringLiteral( "pending migration source does not match active storage" ) );
            return false;
        }
        if ( existing.state->pending.has_value() ) {
            if ( sameRequest( *existing.state->pending, normalized ) ) {
                return true;
            }
            setError( error, QStringLiteral( "a different storage migration is already pending" ) );
            return false;
        }
    }
    else if ( current.state.has_value() ) {
        setError( error, QStringLiteral( "pending migration source locator is no longer active" ) );
        return false;
    }
    const bool verified = existing.state.has_value() ? existing.state->verified : false;
    normalized.sourceLocatorExisted = existing.found;
    return writeState( normalized.source.locatorPath,
                       StorageLocatorState{ 1, normalized.source, normalized, verified }, paths,
                       error );
}

bool StorageLocatorStore::commitPending( const StorageMigrationRequest& request,
                                         QString* error ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    StorageMigrationRequest normalized;
    if ( !normalizedRequest( request, paths, &normalized, error ) ) {
        return false;
    }
    auto mutationLock = lockLocatorMutations( paths, error );
    if ( !mutationLock ) {
        return false;
    }
    const ReadResult source = readState( normalized.source.locatorPath, paths );
    if ( !source.state.has_value() || !source.state->pending.has_value()
         || !sameRequest( *source.state->pending, normalized ) ) {
        setError( error, source.error.isEmpty()
                             ? QStringLiteral( "pending storage migration does not match" )
                             : source.error );
        return false;
    }
    const StorageMigrationRequest& pending = *source.state->pending;
    const StorageLocatorState targetState{ 1, pending.target, std::nullopt, false };
    if ( samePath( pending.source.locatorPath, pending.target.locatorPath ) ) {
        return writeState(
            pending.target.locatorPath, targetState, paths, error,
            LastMigrationRecord{ pending.transactionId, QStringLiteral( "committed" ) } );
    }
    const FileSnapshot previousTarget = snapshot( pending.target.locatorPath );
    if ( !previousTarget.readable ) {
        setError( error, QStringLiteral( "failed to read existing storage locator: %1" )
                             .arg( pending.target.locatorPath ) );
        return false;
    }
    if ( !writeState(
             pending.target.locatorPath, targetState, paths, error,
             LastMigrationRecord{ pending.transactionId, QStringLiteral( "committed" ) } ) ) {
        return false;
    }
    if ( !QFile::remove( pending.source.locatorPath ) ) {
        QString restoreError;
        const bool restored = restore( pending.target.locatorPath, previousTarget, &restoreError );
        setError( error, restored
                             ? QStringLiteral( "failed to remove source storage locator: %1" )
                                   .arg( pending.source.locatorPath )
                             : QStringLiteral( "failed to remove source storage locator: %1; %2" )
                                   .arg( pending.source.locatorPath, restoreError ) );
        return false;
    }
    return true;
}

bool StorageLocatorStore::rollbackPending( const StorageMigrationRequest& request,
                                           QString* error ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    StorageMigrationRequest normalized;
    if ( !normalizedRequest( request, paths, &normalized, error ) ) {
        return false;
    }
    auto mutationLock = lockLocatorMutations( paths, error );
    if ( !mutationLock ) {
        return false;
    }
    const ReadResult source = readState( normalized.source.locatorPath, paths );
    if ( !source.state.has_value() || !source.state->pending.has_value()
         || !sameRequest( *source.state->pending, normalized ) ) {
        setError( error, source.error.isEmpty()
                             ? QStringLiteral( "pending storage migration does not match" )
                             : source.error );
        return false;
    }
    const StorageMigrationRequest& pending = *source.state->pending;
    if ( !pending.sourceLocatorExisted ) {
        if ( QFile::remove( normalized.source.locatorPath ) ) {
            return true;
        }
        setError( error, QStringLiteral( "failed to remove newly created source locator: %1" )
                             .arg( normalized.source.locatorPath ) );
        return false;
    }
    return writeState(
        normalized.source.locatorPath,
        StorageLocatorState{ 1, pending.source, std::nullopt, source.state->verified }, paths,
        error, LastMigrationRecord{ pending.transactionId, QStringLiteral( "rolledBack" ) } );
}

bool StorageLocatorStore::discardUnverifiedLegacyLocator( const StorageLocation& location,
                                                          QString* error ) const
{
    const LocatorPaths paths{ applicationDirectory_, programLocatorPath(), userLocatorPath() };
    const QString targetPath = pathForActiveLocation( location, paths );
    StorageLocation normalized;
    if ( !normalizedLocation( location, targetPath, paths, &normalized, error ) ) {
        return false;
    }
    auto mutationLock = lockLocatorMutations( paths, error );
    if ( !mutationLock ) {
        return false;
    }
    const ReadResult existing = readState( targetPath, paths );
    if ( !existing.state.has_value() ) {
        setError( error,
                  existing.error.isEmpty()
                      ? QStringLiteral( "legacy storage locator is missing: %1" ).arg( targetPath )
                      : existing.error );
        return false;
    }
    if ( existing.state->verified || existing.state->pending.has_value()
         || !sameLocation( existing.state->active, normalized ) ) {
        setError( error, QStringLiteral( "legacy storage locator is not safely discardable: %1" )
                             .arg( targetPath ) );
        return false;
    }
    QSettings settings{ targetPath, QSettings::IniFormat };
    if ( settings.value( QStringLiteral( "LastMigration/outcome" ) ).toString()
             != QStringLiteral( "rolledBack" )
         || settings.value( QStringLiteral( "LastMigration/transactionId" ) ).toString().isEmpty()
         || settings.status() != QSettings::NoError ) {
        setError( error,
                  QStringLiteral( "legacy storage locator has no rolled-back transaction: %1" )
                      .arg( targetPath ) );
        return false;
    }
    if ( QFile::remove( targetPath ) ) {
        return true;
    }
    setError(
        error,
        QStringLiteral( "failed to discard rolled-back legacy locator: %1" ).arg( targetPath ) );
    return false;
}
