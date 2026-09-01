#include "storagemigrator.h"

#include "storagevalidator.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <utility>

namespace {

struct CreatedTargets {
    QStringList files;
    QStringList directories;
};

struct DirectLocatorState {
    bool found = false;
    bool valid = false;
    StorageLocation active;
    std::optional<StorageMigrationRequest> pending;
    QString lastTransactionId;
    QString lastOutcome;
    QString error;
};

QString normalized( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool pathExistsOrIsSymlink( const QString& path )
{
    const QFileInfo info{ path };
    return info.exists() || info.isSymLink();
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool samePath( const QString& left, const QString& right )
{
    return normalized( left ).compare( normalized( right ), pathCaseSensitivity() ) == 0;
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
           && samePath( left.legacyCrashDirectory, right.legacyCrashDirectory );
}

bool sameOrChildPath( const QString& path, const QString& parent )
{
    const QString cleanPath = normalized( path );
    QString cleanParent = normalized( parent );
    if ( samePath( cleanPath, cleanParent ) ) {
        return true;
    }
    if ( !cleanParent.endsWith( QLatin1Char( '/' ) ) ) {
        cleanParent.append( QLatin1Char( '/' ) );
    }
    return cleanPath.startsWith( cleanParent, pathCaseSensitivity() );
}

QString expectedLocatorPath( StorageMode mode, const StorageLocatorStore& store )
{
    return mode == StorageMode::ProgramDirectory ? store.programLocatorPath()
                                                 : store.userLocatorPath();
}

bool canonicalLocation( const StorageLocation& input, const StorageLocatorStore& store,
                        StorageLocation* output, QString* error )
{
    if ( input.commandLineOverride ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "command-line storage locations cannot be migrated" );
        }
        return false;
    }
    const QString expectedLocator = normalized( expectedLocatorPath( input.mode, store ) );
    if ( !input.locatorPath.isEmpty() && !samePath( input.locatorPath, expectedLocator ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "storage migration location uses the wrong locator: %1" )
                         .arg( input.locatorPath );
        }
        return false;
    }

    const QString requestedRoot = normalized( input.dataRoot );
    QString root;
    if ( requestedRoot == QStringLiteral( "data" )
         && input.mode == StorageMode::ProgramDirectory ) {
        root = normalized( QDir{ QFileInfo{ store.programLocatorPath() }.absolutePath() }.filePath(
            QStringLiteral( "data" ) ) );
    }
    else if ( QFileInfo{ requestedRoot }.isAbsolute() ) {
        root = requestedRoot;
    }
    else {
        if ( error != nullptr ) {
            *error = QStringLiteral( "storage migration data root must be absolute: %1" )
                         .arg( input.dataRoot );
        }
        return false;
    }
    *output = { input.mode, root, expectedLocator, false };
    return true;
}

bool canonicalLegacyPath( const QString& input, const QString& label, QString* output,
                          QString* error )
{
    if ( input.isEmpty() ) {
        output->clear();
        return true;
    }
    const QString path = normalized( input );
    if ( !QFileInfo{ path }.isAbsolute() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "%1 must be an absolute path: %2" ).arg( label, input );
        }
        return false;
    }
    *output = path;
    return true;
}

bool validateNoSourceTargetConflicts( const StorageMigrationRequest& request, QString* error )
{
    const StorageContext targetContext{ request.target };
    if ( !request.legacyCrashDirectory.isEmpty()
         && sameOrChildPath( targetContext.dataRoot(), request.legacyCrashDirectory ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "target storage conflicts with legacy crash path: %1" )
                         .arg( request.legacyCrashDirectory );
        }
        return false;
    }

    const QStringList outputs{ targetContext.dataRoot(),         targetContext.configDirectory(),
                               targetContext.sessionDirectory(), targetContext.logsDirectory(),
                               targetContext.crashesDirectory(), targetContext.configFilePath(),
                               targetContext.sessionFilePath(),  targetContext.manifestFilePath() };
    const QStringList sources{ request.legacyConfigFile, request.legacySessionFile };
    for ( const QString& source : sources ) {
        if ( source.isEmpty() ) {
            continue;
        }
        for ( const QString& output : outputs ) {
            if ( samePath( source, output ) ) {
                if ( error != nullptr ) {
                    *error = QStringLiteral( "legacy INI path conflicts with target output: %1" )
                                 .arg( source );
                }
                return false;
            }
        }
    }
    return true;
}

bool canonicalRequest( const StorageMigrationRequest& input, const StorageLocatorStore& store,
                       StorageMigrationRequest* output, QString* error )
{
    if ( input.transactionId.isEmpty() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "storage migration transaction id is empty" );
        }
        return false;
    }
    StorageLocation source;
    StorageLocation target;
    if ( !canonicalLocation( input.source, store, &source, error )
         || !canonicalLocation( input.target, store, &target, error ) ) {
        return false;
    }
    QString legacyConfigFile;
    QString legacySessionFile;
    QString legacyCrashDirectory;
    if ( !canonicalLegacyPath( input.legacyConfigFile, QStringLiteral( "legacy config file" ),
                               &legacyConfigFile, error )
         || !canonicalLegacyPath( input.legacySessionFile, QStringLiteral( "legacy session file" ),
                                  &legacySessionFile, error )
         || !canonicalLegacyPath( input.legacyCrashDirectory,
                                  QStringLiteral( "legacy crash directory" ), &legacyCrashDirectory,
                                  error ) ) {
        return false;
    }
    *output = { input.transactionId, source, target, legacyConfigFile, legacySessionFile,
                legacyCrashDirectory };
    return validateNoSourceTargetConflicts( *output, error );
}

bool modeFromString( const QString& value, StorageMode* mode )
{
    if ( value == QStringLiteral( "program" ) ) {
        *mode = StorageMode::ProgramDirectory;
        return true;
    }
    if ( value == QStringLiteral( "user" ) ) {
        *mode = StorageMode::UserDirectory;
        return true;
    }
    if ( value == QStringLiteral( "custom" ) ) {
        *mode = StorageMode::CustomDirectory;
        return true;
    }
    return false;
}

DirectLocatorState readDirectLocator( const QString& locatorPath, const StorageLocatorStore& store )
{
    const QString path = normalized( locatorPath );
    const QFileInfo info{ path };
    if ( !info.exists() ) {
        return {};
    }
    if ( !info.isFile() ) {
        DirectLocatorState state;
        state.found = true;
        state.error = QStringLiteral( "storage locator is not a file: %1" ).arg( path );
        return state;
    }

    QSettings settings{ path, QSettings::IniFormat };
    bool versionOk = false;
    const int version
        = settings.value( QStringLiteral( "Storage/formatVersion" ) ).toInt( &versionOk );
    StorageMode mode = StorageMode::UserDirectory;
    if ( !versionOk || version != 1
         || !modeFromString( settings.value( QStringLiteral( "Storage/mode" ) ).toString(),
                             &mode ) ) {
        DirectLocatorState state;
        state.found = true;
        state.error
            = QStringLiteral( "storage locator has invalid storage metadata: %1" ).arg( path );
        return state;
    }
    StorageLocation activeInput{ mode,
                                 settings.value( QStringLiteral( "Storage/dataRoot" ) ).toString(),
                                 path, false };
    StorageLocation active;
    QString locationError;
    if ( !canonicalLocation( activeInput, store, &active, &locationError ) ) {
        DirectLocatorState state;
        state.found = true;
        state.error = QStringLiteral( "%1: %2" ).arg( locationError, path );
        return state;
    }

    DirectLocatorState state;
    state.found = true;
    state.active = active;
    const QStringList groups = settings.childGroups();
    if ( groups.contains( QStringLiteral( "Pending" ) )
         && groups.contains( QStringLiteral( "LastMigration" ) ) ) {
        state.error
            = QStringLiteral( "storage locator cannot contain both Pending and LastMigration: %1" )
                  .arg( path );
        return state;
    }
    if ( groups.contains( QStringLiteral( "Pending" ) ) ) {
        const QStringList keys{ QStringLiteral( "Pending/transactionId" ),
                                QStringLiteral( "Pending/sourceMode" ),
                                QStringLiteral( "Pending/sourceRoot" ),
                                QStringLiteral( "Pending/sourceLocator" ),
                                QStringLiteral( "Pending/targetMode" ),
                                QStringLiteral( "Pending/targetRoot" ),
                                QStringLiteral( "Pending/targetLocator" ),
                                QStringLiteral( "Pending/legacyConfigFile" ),
                                QStringLiteral( "Pending/legacySessionFile" ),
                                QStringLiteral( "Pending/legacyCrashDirectory" ) };
        for ( const QString& key : keys ) {
            if ( !settings.contains( key ) ) {
                state.error = QStringLiteral( "storage locator Pending is missing %1: %2" )
                                  .arg( key, path );
                return state;
            }
        }
        StorageMode sourceMode = StorageMode::UserDirectory;
        StorageMode targetMode = StorageMode::UserDirectory;
        const QString sourceLocator
            = normalized( settings.value( QStringLiteral( "Pending/sourceLocator" ) ).toString() );
        const QString targetLocator
            = normalized( settings.value( QStringLiteral( "Pending/targetLocator" ) ).toString() );
        if ( !modeFromString( settings.value( QStringLiteral( "Pending/sourceMode" ) ).toString(),
                              &sourceMode )
             || !modeFromString(
                 settings.value( QStringLiteral( "Pending/targetMode" ) ).toString(), &targetMode )
             || !samePath( sourceLocator, path ) ) {
            state.error
                = QStringLiteral( "storage locator has invalid Pending locations: %1" ).arg( path );
            return state;
        }
        StorageMigrationRequest serialized{
            settings.value( QStringLiteral( "Pending/transactionId" ) ).toString(),
            { sourceMode, settings.value( QStringLiteral( "Pending/sourceRoot" ) ).toString(),
              sourceLocator, false },
            { targetMode, settings.value( QStringLiteral( "Pending/targetRoot" ) ).toString(),
              targetLocator, false },
            settings.value( QStringLiteral( "Pending/legacyConfigFile" ) ).toString(),
            settings.value( QStringLiteral( "Pending/legacySessionFile" ) ).toString(),
            settings.value( QStringLiteral( "Pending/legacyCrashDirectory" ) ).toString()
        };
        StorageMigrationRequest pending;
        QString pendingError;
        if ( !canonicalRequest( serialized, store, &pending, &pendingError )
             || !sameLocation( pending.source, active ) ) {
            state.error = QStringLiteral( "invalid pending storage migration: %1: %2" )
                              .arg( pendingError, path );
            return state;
        }
        state.pending = pending;
    }
    if ( groups.contains( QStringLiteral( "LastMigration" ) ) ) {
        const QString transactionId
            = settings.value( QStringLiteral( "LastMigration/transactionId" ) ).toString();
        const QString outcome
            = settings.value( QStringLiteral( "LastMigration/outcome" ) ).toString();
        if ( transactionId.isEmpty()
             || ( outcome != QStringLiteral( "committed" )
                  && outcome != QStringLiteral( "rolledBack" ) ) ) {
            state.error = QStringLiteral( "storage locator has invalid LastMigration metadata: %1" )
                              .arg( path );
            return state;
        }
        state.lastTransactionId = transactionId;
        state.lastOutcome = outcome;
    }
    if ( settings.status() != QSettings::NoError ) {
        state.error = QStringLiteral( "failed to read storage locator: %1" ).arg( path );
        return state;
    }
    state.valid = true;
    return state;
}

bool targetRemainsCommitted( const StorageMigrationRequest& request,
                             const StorageLocatorStore& store )
{
    const DirectLocatorState targetState = readDirectLocator( request.target.locatorPath, store );
    return targetState.found && targetState.valid && !targetState.pending.has_value()
           && targetState.lastTransactionId == request.transactionId
           && targetState.lastOutcome == QStringLiteral( "committed" )
           && sameLocation( targetState.active, request.target );
}

void appendUnique( QStringList& paths, const QString& path )
{
    const QString cleanPath = normalized( path );
    const bool alreadyPresent
        = std::any_of( paths.cbegin(), paths.cend(), [ &cleanPath ]( const QString& existing ) {
              return samePath( existing, cleanPath );
          } );
    if ( !alreadyPresent ) {
        paths.append( cleanPath );
    }
}

bool defaultCopy( const QString& source, const QString& target, QString* error )
{
    QFile input{ source };
    if ( !input.open( QIODevice::ReadOnly ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to open source file for reading" );
        }
        return false;
    }

    QSaveFile output{ target };
    if ( !output.open( QIODevice::WriteOnly ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to open target file for atomic writing" );
        }
        return false;
    }

    while ( !input.atEnd() ) {
        const QByteArray bytes = input.read( 64 * 1024 );
        if ( bytes.isEmpty() && input.error() != QFileDevice::NoError ) {
            if ( error != nullptr ) {
                *error = QStringLiteral( "failed while reading source file" );
            }
            return false;
        }
        if ( output.write( bytes ) != bytes.size() ) {
            if ( error != nullptr ) {
                *error = QStringLiteral( "failed while writing target file" );
            }
            return false;
        }
    }
    if ( !output.commit() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to commit target file atomically" );
        }
        return false;
    }
    return true;
}

bool ensureTargetDirectories( const StorageContext& context, CreatedTargets* created,
                              QString* error )
{
    const QStringList directories{ context.dataRoot(), context.configDirectory(),
                                   context.sessionDirectory(), context.logsDirectory(),
                                   context.crashesDirectory() };
    QStringList absent;
    for ( const QString& directory : directories ) {
        if ( !pathExistsOrIsSymlink( directory ) ) {
            absent.append( directory );
        }
    }

    const bool ensured = context.ensureDirectories( error );
    for ( const QString& directory : absent ) {
        if ( QFileInfo{ directory }.isDir() ) {
            appendUnique( created->directories, directory );
        }
    }
    return ensured;
}

bool createEmptyIni( const QString& target, CreatedTargets* created, QString* error )
{
    const bool existed = pathExistsOrIsSymlink( target );
    QSaveFile output{ target };
    const QByteArray contents{ "[General]\n" };
    if ( !output.open( QIODevice::WriteOnly ) || output.write( contents ) != contents.size()
         || !output.commit() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to atomically create empty INI: %1" ).arg( target );
        }
        return false;
    }
    if ( !existed ) {
        appendUnique( created->files, target );
    }
    return true;
}

bool copyLegacyIni( const QString& source, const QString& target,
                    const StorageCopyOperation& copyOperation, CreatedTargets* created,
                    QString* error )
{
    if ( pathExistsOrIsSymlink( target ) ) {
        if ( error != nullptr ) {
            *error
                = QStringLiteral( "refusing to overwrite existing migration target from %1 to %2" )
                      .arg( source, target );
        }
        return false;
    }
    const QFileInfo sourceInfo{ source };
    if ( !sourceInfo.exists() ) {
        QString detail;
        if ( createEmptyIni( target, created, &detail ) ) {
            return true;
        }
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to create empty legacy INI from missing %1 to %2: %3" )
                         .arg( source, target, detail );
        }
        return false;
    }
    if ( !sourceInfo.isFile() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "legacy INI source is not a file: %1" ).arg( source );
        }
        return false;
    }

    const bool existed = pathExistsOrIsSymlink( target );
    QString detail;
    if ( !copyOperation( source, target, &detail ) ) {
        if ( !existed && pathExistsOrIsSymlink( target ) ) {
            appendUnique( created->files, target );
        }
        if ( error != nullptr ) {
            *error = QStringLiteral( "failed to copy legacy INI from %1 to %2: %3" )
                         .arg( source, target, detail );
        }
        return false;
    }
    if ( !existed ) {
        appendUnique( created->files, target );
    }
    return true;
}

bool ensureCrashDirectory( const QString& path, CreatedTargets* created, QString* error )
{
    if ( QFileInfo{ path }.isDir() ) {
        return true;
    }
    if ( QDir{}.mkpath( path ) ) {
        appendUnique( created->directories, path );
        return true;
    }
    if ( error != nullptr ) {
        *error = QStringLiteral( "failed to create crash target directory: %1" ).arg( path );
    }
    return false;
}

bool copyCrashTree( const QString& sourceRoot, const QString& targetRoot,
                    const StorageCopyOperation& copyOperation, CreatedTargets* created,
                    QString* error )
{
    const QFileInfo rootInfo{ sourceRoot };
    if ( rootInfo.isSymLink() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "refusing symbolic link in legacy crash tree: %1" )
                         .arg( sourceRoot );
        }
        return false;
    }
    if ( !rootInfo.exists() ) {
        return true;
    }
    if ( !rootInfo.isDir() ) {
        if ( error != nullptr ) {
            *error
                = QStringLiteral( "legacy crash source is not a directory: %1" ).arg( sourceRoot );
        }
        return false;
    }

    const QDir sourceDirectory{ sourceRoot };
    QDirIterator iterator{ sourceRoot,
                           QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
                           QDirIterator::Subdirectories };
    while ( iterator.hasNext() ) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString source = normalized( info.filePath() );
        if ( info.isSymLink() ) {
            if ( error != nullptr ) {
                *error = QStringLiteral( "refusing symbolic link in legacy crash tree: %1" )
                             .arg( source );
            }
            return false;
        }

        const QString relative = sourceDirectory.relativeFilePath( source );
        const QString target = normalized( QDir{ targetRoot }.filePath( relative ) );
        if ( info.isDir() ) {
            if ( !ensureCrashDirectory( target, created, error ) ) {
                return false;
            }
            continue;
        }
        if ( !info.isFile() ) {
            if ( error != nullptr ) {
                *error
                    = QStringLiteral( "unsupported entry in legacy crash tree: %1" ).arg( source );
            }
            return false;
        }
        if ( !ensureCrashDirectory( QFileInfo{ target }.absolutePath(), created, error ) ) {
            return false;
        }
        const bool existed = pathExistsOrIsSymlink( target );
        if ( existed ) {
            if ( error != nullptr ) {
                *error = QStringLiteral(
                             "refusing to overwrite existing legacy crash target from %1 to %2" )
                             .arg( source, target );
            }
            return false;
        }
        QString detail;
        if ( !copyOperation( source, target, &detail ) ) {
            if ( !existed && pathExistsOrIsSymlink( target ) ) {
                appendUnique( created->files, target );
            }
            if ( error != nullptr ) {
                *error = QStringLiteral( "failed to copy legacy crash file from %1 to %2: %3" )
                             .arg( source, target, detail );
            }
            return false;
        }
        if ( !existed ) {
            appendUnique( created->files, target );
        }
    }
    return true;
}

bool readableIni( const QString& path, QString* error )
{
    const QFileInfo info{ path };
    if ( !info.isFile() ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "target INI is missing or not a file: %1" ).arg( path );
        }
        return false;
    }
    QFile file{ path };
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "target INI is not readable: %1" ).arg( path );
        }
        return false;
    }
    while ( !file.atEnd() ) {
        file.read( 64 * 1024 );
        if ( file.error() != QFileDevice::NoError ) {
            if ( error != nullptr ) {
                *error = QStringLiteral( "failed while reading target INI: %1" ).arg( path );
            }
            return false;
        }
    }
    QSettings settings{ path, QSettings::IniFormat };
    settings.allKeys();
    if ( settings.status() != QSettings::NoError ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "target INI has invalid format: %1" ).arg( path );
        }
        return false;
    }
    return true;
}

QString cleanupCreatedTargets( const CreatedTargets& created )
{
    QStringList failedPaths;
    for ( auto iterator = created.files.crbegin(); iterator != created.files.crend(); ++iterator ) {
        if ( pathExistsOrIsSymlink( *iterator ) && !QFile::remove( *iterator ) ) {
            failedPaths.append( *iterator );
        }
    }
    for ( auto iterator = created.directories.crbegin(); iterator != created.directories.crend();
          ++iterator ) {
        if ( pathExistsOrIsSymlink( *iterator ) && !QDir{}.rmdir( *iterator ) ) {
            failedPaths.append( *iterator );
        }
    }
    return failedPaths.isEmpty() ? QString{}
                                 : QStringLiteral( "cleanup failed for transaction paths: %1" )
                                       .arg( failedPaths.join( QStringLiteral( ", " ) ) );
}

StorageMigrationResult rollbackFailure( const StorageLocatorStore& locatorStore,
                                        const StorageMigrationRequest& request,
                                        const StorageContext&, const CreatedTargets& created,
                                        const QString& originalError )
{
    QString rollbackError;
    const bool locatorRolledBack = locatorStore.rollbackPending( request, &rollbackError );
    const QString cleanupError = cleanupCreatedTargets( created );
    if ( locatorRolledBack && cleanupError.isEmpty() ) {
        return { false, true, originalError };
    }
    QStringList errors{ originalError };
    if ( !locatorRolledBack ) {
        errors.append( QStringLiteral( "rollback failed: %1" ).arg( rollbackError ) );
    }
    if ( !cleanupError.isEmpty() ) {
        errors.append( cleanupError );
    }
    return { false, false, errors.join( QStringLiteral( "; " ) ) };
}

bool ensureLockParent( const QString& lockPath, QString* error )
{
    const QString parent = QFileInfo{ lockPath }.absolutePath();
    if ( QDir{}.mkpath( parent ) ) {
        return true;
    }
    if ( error != nullptr ) {
        *error = QStringLiteral( "failed to create storage migration lock directory for %1" )
                     .arg( lockPath );
    }
    return false;
}

QString lockFailure( const QLockFile& lock, const QString& lockPath )
{
    switch ( lock.error() ) {
    case QLockFile::LockFailedError:
        return QStringLiteral( "storage migration lock is already held: %1" ).arg( lockPath );
    case QLockFile::PermissionError:
        return QStringLiteral( "permission denied acquiring storage migration lock: %1" )
            .arg( lockPath );
    case QLockFile::UnknownError:
        return QStringLiteral( "unknown error acquiring storage migration lock: %1" )
            .arg( lockPath );
    case QLockFile::NoError:
        break;
    }
    return QStringLiteral( "failed to acquire storage migration lock: %1" ).arg( lockPath );
}

} // namespace

StorageMigrator::StorageMigrator( StorageLocatorStore locatorStore,
                                  StorageCopyOperation copyOperation )
    : locatorStore_( std::move( locatorStore ) )
    , copyOperation_( copyOperation ? std::move( copyOperation )
                                    : StorageCopyOperation{ defaultCopy } )
{
}

StorageMigrationResult StorageMigrator::execute( const StorageMigrationRequest& request ) const
{
    QString error;
    StorageMigrationRequest canonical;
    if ( !canonicalRequest( request, locatorStore_, &canonical, &error ) ) {
        return { false, false,
                 QStringLiteral( "invalid storage migration request: %1" ).arg( error ) };
    }

    const QString lockPath = canonical.source.locatorPath + QStringLiteral( ".migration.lock" );
    if ( !ensureLockParent( lockPath, &error ) ) {
        return { false, false, error };
    }
    QLockFile lock{ lockPath };
    lock.setStaleLockTime( 0 );
    if ( !lock.tryLock( 0 ) ) {
        return { false, false, lockFailure( lock, lockPath ) };
    }

    if ( !locatorStore_.writePending( canonical, &error ) ) {
        return { false, false,
                 QStringLiteral( "failed to write pending storage migration: %1" ).arg( error ) };
    }

    const StorageContext targetContext{ canonical.target };
    CreatedTargets created;
    if ( !ensureTargetDirectories( targetContext, &created, &error ) ) {
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( pathExistsOrIsSymlink( targetContext.manifestFilePath() ) ) {
        return rollbackFailure(
            locatorStore_, canonical, targetContext, created,
            QStringLiteral( "refusing to overwrite existing storage manifest: %1" )
                .arg( targetContext.manifestFilePath() ) );
    }
    if ( !copyLegacyIni( canonical.legacyConfigFile, targetContext.configFilePath(), copyOperation_,
                         &created, &error ) ) {
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( !copyLegacyIni( canonical.legacySessionFile, targetContext.sessionFilePath(),
                         copyOperation_, &created, &error ) ) {
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( !copyCrashTree( canonical.legacyCrashDirectory, targetContext.crashesDirectory(),
                         copyOperation_, &created, &error ) ) {
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( !readableIni( targetContext.configFilePath(), &error )
         || !readableIni( targetContext.sessionFilePath(), &error ) ) {
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }

    const bool manifestExisted = pathExistsOrIsSymlink( targetContext.manifestFilePath() );
    if ( !StorageValidator::writeManifest( targetContext, &error ) ) {
        if ( !manifestExisted && pathExistsOrIsSymlink( targetContext.manifestFilePath() ) ) {
            appendUnique( created.files, targetContext.manifestFilePath() );
        }
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( !manifestExisted ) {
        appendUnique( created.files, targetContext.manifestFilePath() );
    }

    if ( !locatorStore_.commitPending( canonical, &error ) ) {
        const QString commitError
            = QStringLiteral( "failed to commit pending storage migration: %1" ).arg( error );
        if ( targetRemainsCommitted( canonical, locatorStore_ ) ) {
            return { false, false,
                     QStringLiteral( "%1; target locator remains committed; recovery required: %2" )
                         .arg( commitError, canonical.target.locatorPath ) };
        }
        return rollbackFailure( locatorStore_, canonical, targetContext, created, commitError );
    }
    return { true, false, {} };
}

StorageMigrationResult
StorageMigrator::recoverPending( const StorageMigrationRequest& request ) const
{
    QString error;
    StorageMigrationRequest canonical;
    if ( !canonicalRequest( request, locatorStore_, &canonical, &error ) ) {
        return { false, false,
                 QStringLiteral( "invalid storage migration request: %1" ).arg( error ) };
    }

    const QString lockPath = canonical.source.locatorPath + QStringLiteral( ".migration.lock" );
    if ( !ensureLockParent( lockPath, &error ) ) {
        return { false, false, error };
    }
    QLockFile lock{ lockPath };
    lock.setStaleLockTime( 0 );
    if ( !lock.tryLock( 0 ) ) {
        return { false, false, lockFailure( lock, lockPath ) };
    }

    const DirectLocatorState sourceState
        = readDirectLocator( canonical.source.locatorPath, locatorStore_ );
    if ( sourceState.found && !sourceState.valid ) {
        return { false, false, sourceState.error };
    }
    if ( sourceState.pending.has_value() && !sameRequest( *sourceState.pending, canonical ) ) {
        return { false, false,
                 QStringLiteral( "pending storage migration does not match transaction request" ) };
    }
    if ( !sourceState.pending.has_value() ) {
        if ( sourceState.found && sourceState.lastTransactionId == canonical.transactionId ) {
            if ( sourceState.lastOutcome == QStringLiteral( "committed" )
                 && sameLocation( sourceState.active, canonical.target ) ) {
                return { true, false, {} };
            }
            if ( sourceState.lastOutcome == QStringLiteral( "rolledBack" )
                 && sameLocation( sourceState.active, canonical.source ) ) {
                return { false, true,
                         QStringLiteral( "storage migration was already rolled back" ) };
            }
            return { false, false,
                     QStringLiteral( "last migration outcome conflicts with active storage" ) };
        }
        const DirectLocatorState targetState
            = readDirectLocator( canonical.target.locatorPath, locatorStore_ );
        if ( targetState.found && !targetState.valid ) {
            return { false, false, targetState.error };
        }
        if ( targetState.found && !targetState.pending.has_value()
             && targetState.lastTransactionId == canonical.transactionId
             && targetState.lastOutcome == QStringLiteral( "committed" )
             && sameLocation( targetState.active, canonical.target ) ) {
            return { true, false, {} };
        }
        return { false, false,
                 QStringLiteral(
                     "storage migration transaction has no matching pending or last outcome" ) };
    }

    const StorageContext targetContext{ canonical.target };
    QString configError;
    QString sessionError;
    const bool complete = StorageValidator::hasCompatibleManifest( targetContext.dataRoot() )
                          && readableIni( targetContext.configFilePath(), &configError )
                          && readableIni( targetContext.sessionFilePath(), &sessionError );
    if ( complete ) {
        if ( locatorStore_.commitPending( canonical, &error ) ) {
            return { true, false, {} };
        }
        const QString commitError
            = QStringLiteral( "failed to recover pending storage migration: %1" ).arg( error );
        if ( targetRemainsCommitted( canonical, locatorStore_ ) ) {
            return { false, false,
                     QStringLiteral( "%1; target locator remains committed; recovery required: %2" )
                         .arg( commitError, canonical.target.locatorPath ) };
        }
        QString rollbackError;
        if ( locatorStore_.rollbackPending( canonical, &rollbackError ) ) {
            return { false, true, commitError };
        }
        return { false, false,
                 QStringLiteral( "%1; rollback failed: %2" ).arg( commitError, rollbackError ) };
    }

    if ( locatorStore_.rollbackPending( canonical, &error ) ) {
        return { false, true,
                 QStringLiteral( "incomplete pending storage migration rolled back" ) };
    }
    return { false, false,
             QStringLiteral( "failed to roll back incomplete pending storage migration: %1" )
                 .arg( error ) };
}
