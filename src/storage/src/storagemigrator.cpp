#include "storagemigrator.h"

#include "storagevalidator.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>

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
    bool hasPending = false;
    QString error;
};

QString normalized( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool sameLocation( const StorageLocation& left, const StorageLocation& right )
{
    return left.mode == right.mode && normalized( left.dataRoot ) == normalized( right.dataRoot )
           && normalized( left.locatorPath ) == normalized( right.locatorPath )
           && left.commandLineOverride == right.commandLineOverride;
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
    if ( !input.locatorPath.isEmpty() && normalized( input.locatorPath ) != expectedLocator ) {
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

bool canonicalRequest( const StorageMigrationRequest& input, const StorageLocatorStore& store,
                       StorageMigrationRequest* output, QString* error )
{
    StorageLocation source;
    StorageLocation target;
    if ( !canonicalLocation( input.source, store, &source, error )
         || !canonicalLocation( input.target, store, &target, error ) ) {
        return false;
    }
    *output = {
        input.transactionId,       source, target, input.legacyConfigFile, input.legacySessionFile,
        input.legacyCrashDirectory
    };
    return true;
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
        return { true,
                 false,
                 {},
                 false,
                 QStringLiteral( "storage locator is not a file: %1" ).arg( path ) };
    }

    QSettings settings{ path, QSettings::IniFormat };
    bool versionOk = false;
    const int version
        = settings.value( QStringLiteral( "Storage/formatVersion" ) ).toInt( &versionOk );
    StorageMode mode = StorageMode::UserDirectory;
    if ( !versionOk || version != 1
         || !modeFromString( settings.value( QStringLiteral( "Storage/mode" ) ).toString(),
                             &mode ) ) {
        return { true,
                 false,
                 {},
                 false,
                 QStringLiteral( "storage locator has invalid storage metadata: %1" ).arg( path ) };
    }
    StorageLocation activeInput{ mode,
                                 settings.value( QStringLiteral( "Storage/dataRoot" ) ).toString(),
                                 path, false };
    StorageLocation active;
    QString locationError;
    if ( !canonicalLocation( activeInput, store, &active, &locationError ) ) {
        return { true, false, {}, false, QStringLiteral( "%1: %2" ).arg( locationError, path ) };
    }
    const bool hasPending = settings.childGroups().contains( QStringLiteral( "Pending" ) );
    if ( settings.status() != QSettings::NoError ) {
        return { true,
                 false,
                 {},
                 false,
                 QStringLiteral( "failed to read storage locator: %1" ).arg( path ) };
    }
    return { true, true, active, hasPending, {} };
}

void appendUnique( QStringList& paths, const QString& path )
{
    const QString cleanPath = normalized( path );
    if ( !paths.contains( cleanPath ) ) {
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
        if ( !QFileInfo{ directory }.exists() ) {
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
    const bool existed = QFileInfo{ target }.exists();
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
    if ( QFileInfo{ target }.exists() ) {
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

    const bool existed = QFileInfo{ target }.exists();
    QString detail;
    if ( !copyOperation( source, target, &detail ) ) {
        if ( !existed && QFileInfo{ target }.exists() ) {
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
        const bool existed = QFileInfo{ target }.exists();
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
            if ( !existed && QFileInfo{ target }.exists() ) {
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

void cleanupCreatedTargets( const StorageContext& context, const CreatedTargets& created )
{
    if ( StorageValidator::hasCompatibleManifest( context.dataRoot() ) ) {
        return;
    }
    for ( auto iterator = created.files.crbegin(); iterator != created.files.crend(); ++iterator ) {
        QFile::remove( *iterator );
    }
    for ( auto iterator = created.directories.crbegin(); iterator != created.directories.crend();
          ++iterator ) {
        QDir{}.rmdir( *iterator );
    }
}

StorageMigrationResult rollbackFailure( const StorageLocatorStore& locatorStore,
                                        const StorageMigrationRequest& request,
                                        const StorageContext& context,
                                        const CreatedTargets& created,
                                        const QString& originalError )
{
    QString rollbackError;
    const bool rolledBack = locatorStore.rollbackPending( request, &rollbackError );
    cleanupCreatedTargets( context, created );
    if ( rolledBack ) {
        return { false, true, originalError };
    }
    return { false, false,
             QStringLiteral( "%1; rollback failed: %2" ).arg( originalError, rollbackError ) };
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
    const QString lockPath = normalized( expectedLocatorPath( request.source.mode, locatorStore_ )
                                         + QStringLiteral( ".migration.lock" ) );
    QLockFile lock{ lockPath };
    lock.setStaleLockTime( 0 );
    if ( !lock.tryLock( 0 ) ) {
        return { false, false,
                 QStringLiteral( "failed to acquire storage migration lock: %1" ).arg( lockPath ) };
    }

    QString error;
    StorageMigrationRequest canonical;
    if ( !canonicalRequest( request, locatorStore_, &canonical, &error ) ) {
        return { false, false,
                 QStringLiteral( "invalid storage migration request: %1" ).arg( error ) };
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
    if ( QFileInfo{ targetContext.manifestFilePath() }.exists() ) {
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

    const bool manifestExisted = QFileInfo{ targetContext.manifestFilePath() }.exists();
    if ( !StorageValidator::writeManifest( targetContext, &error ) ) {
        if ( !manifestExisted && QFileInfo{ targetContext.manifestFilePath() }.exists() ) {
            appendUnique( created.files, targetContext.manifestFilePath() );
        }
        return rollbackFailure( locatorStore_, canonical, targetContext, created, error );
    }
    if ( !manifestExisted ) {
        appendUnique( created.files, targetContext.manifestFilePath() );
    }

    if ( !locatorStore_.commitPending( canonical, &error ) ) {
        return rollbackFailure(
            locatorStore_, canonical, targetContext, created,
            QStringLiteral( "failed to commit pending storage migration: %1" ).arg( error ) );
    }
    return { true, false, {} };
}

StorageMigrationResult
StorageMigrator::recoverPending( const StorageMigrationRequest& request ) const
{
    const QString lockPath = normalized( expectedLocatorPath( request.source.mode, locatorStore_ )
                                         + QStringLiteral( ".migration.lock" ) );
    QLockFile lock{ lockPath };
    lock.setStaleLockTime( 0 );
    if ( !lock.tryLock( 0 ) ) {
        return { false, false,
                 QStringLiteral( "failed to acquire storage migration lock: %1" ).arg( lockPath ) };
    }

    QString error;
    StorageMigrationRequest canonical;
    if ( !canonicalRequest( request, locatorStore_, &canonical, &error ) ) {
        return { false, false,
                 QStringLiteral( "invalid storage migration request: %1" ).arg( error ) };
    }
    const DirectLocatorState sourceState
        = readDirectLocator( canonical.source.locatorPath, locatorStore_ );
    if ( sourceState.found && !sourceState.valid ) {
        return { false, false, sourceState.error };
    }
    if ( !sourceState.hasPending ) {
        if ( sourceState.found && sameLocation( sourceState.active, canonical.target ) ) {
            return { true, false, {} };
        }
        if ( sourceState.found && sameLocation( sourceState.active, canonical.source ) ) {
            return { false, true, QStringLiteral( "storage migration was already rolled back" ) };
        }
        const DirectLocatorState targetState
            = readDirectLocator( canonical.target.locatorPath, locatorStore_ );
        if ( targetState.found && !targetState.valid ) {
            return { false, false, targetState.error };
        }
        if ( targetState.found && !targetState.hasPending
             && sameLocation( targetState.active, canonical.target ) ) {
            return { true, false, {} };
        }
        return { false, false,
                 QStringLiteral(
                     "storage migration is no longer pending and active storage does not match" ) };
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
