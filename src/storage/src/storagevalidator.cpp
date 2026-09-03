#include "storagevalidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>

namespace {

QString normalizedAbsolutePath( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    const QString normalized = QDir::cleanPath( QDir::fromNativeSeparators( path ) );
    return QFileInfo{ normalized }.isAbsolute() ? normalized : QString{};
}

void setError( QString* error, const QString& message )
{
    if ( error != nullptr ) {
        *error = message;
    }
}

bool removeProbeFile( const QString& probePath, QString* failedPath )
{
    if ( !QFile::exists( probePath ) ) {
        return true;
    }
    if ( QFile::remove( probePath ) && !QFile::exists( probePath ) ) {
        return true;
    }
    setError( failedPath, probePath );
    return false;
}

bool removeProbeFiles( const QString& writeProbe, const QString& saveProbe, QString* failedPath )
{
    QString failedWritePath;
    QString failedSavePath;
    const bool writeRemoved = removeProbeFile( writeProbe, &failedWritePath );
    const bool saveRemoved = removeProbeFile( saveProbe, &failedSavePath );
    if ( !writeRemoved ) {
        setError( failedPath, failedWritePath );
    }
    else if ( !saveRemoved ) {
        setError( failedPath, failedSavePath );
    }
    return writeRemoved && saveRemoved;
}

QString technicalErrorText( StorageValidationError errorCode, const QStringList& parameters )
{
    const QString parameter = parameters.value( 0 );
    switch ( errorCode ) {
    case StorageValidationError::None:
        return {};
    case StorageValidationError::InvalidRoot:
        return QStringLiteral( "storage directory must be an absolute, non-empty path" );
    case StorageValidationError::NotDirectory:
        return QStringLiteral( "storage path is not a directory: %1" ).arg( parameter );
    case StorageValidationError::CannotCreateDirectory:
        return QStringLiteral( "failed to create storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotWriteDirectory:
        return QStringLiteral( "failed to write storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotReadDirectory:
        return QStringLiteral( "failed to read storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotAtomicallyWriteDirectory:
        return QStringLiteral( "failed to atomically write storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotReadAtomicWrite:
        return QStringLiteral( "failed to read atomic storage write: %1" ).arg( parameter );
    case StorageValidationError::CannotRemoveProbe:
        return QStringLiteral( "failed to remove storage probe file: %1" ).arg( parameter );
    case StorageValidationError::NotEmptyManagedDirectory:
        return QStringLiteral( "storage directory is not an empty managed directory: %1" )
            .arg( parameter );
    }
    return {};
}

StorageValidationResult invalid( const QString& root, StorageValidationError errorCode,
                                 QStringList parameters = {}, bool managedDirectory = false )
{
    return { false, managedDirectory, root, errorCode, parameters,
             technicalErrorText( errorCode, parameters ) };
}

} // namespace

StorageValidationResult StorageValidator::validate( const QString& root,
                                                    bool allowExistingManagedDirectory )
{
    const QString normalizedRoot = normalizedAbsolutePath( root );
    if ( normalizedRoot.isEmpty() ) {
        return invalid( {}, StorageValidationError::InvalidRoot );
    }

    QFileInfo rootInfo{ normalizedRoot };
    if ( rootInfo.exists() && !rootInfo.isDir() ) {
        return invalid( normalizedRoot, StorageValidationError::NotDirectory, { normalizedRoot } );
    }
    if ( !QDir{}.mkpath( normalizedRoot ) ) {
        return invalid( normalizedRoot, StorageValidationError::CannotCreateDirectory,
                        { normalizedRoot } );
    }

    const QString writeProbe = QDir{ normalizedRoot }.filePath(
        QStringLiteral( ".zzlogg-write-test-%1" ).arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) ) );
    const QString saveProbe = QDir{ normalizedRoot }.filePath(
        QStringLiteral( ".zzlogg-write-test-%1" ).arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) ) );
    const QByteArray writeBytes{ "zzlogg-storage-write-check" };
    const QByteArray saveBytes{ "zzlogg-storage-save-check" };
    StorageValidationError probeError = StorageValidationError::None;
    bool probeSucceeded = false;

    do {
        QFile probe{ writeProbe };
        if ( !probe.open( QIODevice::WriteOnly | QIODevice::Truncate )
             || probe.write( writeBytes ) != writeBytes.size() || !probe.flush() ) {
            probeError = StorageValidationError::CannotWriteDirectory;
            break;
        }
        probe.close();

        if ( !probe.open( QIODevice::ReadOnly ) || probe.readAll() != writeBytes ) {
            probeError = StorageValidationError::CannotReadDirectory;
            break;
        }
        probe.close();

        QSaveFile atomicProbe{ saveProbe };
        if ( !atomicProbe.open( QIODevice::WriteOnly )
             || atomicProbe.write( saveBytes ) != saveBytes.size() || !atomicProbe.commit() ) {
            probeError = StorageValidationError::CannotAtomicallyWriteDirectory;
            break;
        }
        QFile savedProbe{ saveProbe };
        if ( !savedProbe.open( QIODevice::ReadOnly ) || savedProbe.readAll() != saveBytes ) {
            probeError = StorageValidationError::CannotReadAtomicWrite;
            break;
        }
        probeSucceeded = true;
    } while ( false );

    QString failedProbePath;
    if ( !removeProbeFiles( writeProbe, saveProbe, &failedProbePath ) ) {
        return invalid( normalizedRoot, StorageValidationError::CannotRemoveProbe,
                        { failedProbePath } );
    }
    if ( !probeSucceeded ) {
        return invalid( normalizedRoot, probeError, { normalizedRoot } );
    }

    const bool managedDirectory = hasCompatibleManifest( normalizedRoot );
    const QStringList remaining = QDir{ normalizedRoot }.entryList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System );
    if ( !remaining.isEmpty() && !( allowExistingManagedDirectory && managedDirectory ) ) {
        return invalid( normalizedRoot, StorageValidationError::NotEmptyManagedDirectory,
                        { normalizedRoot }, managedDirectory );
    }
    return { true, managedDirectory, normalizedRoot, StorageValidationError::None, {}, {} };
}

bool StorageValidator::writeManifest( const StorageContext& context, QString* error )
{
    const QString root = normalizedAbsolutePath( context.dataRoot() );
    if ( root.isEmpty() ) {
        setError( error, QStringLiteral( "storage directory must be an absolute, non-empty path" ) );
        return false;
    }
    if ( !QDir{}.mkpath( root ) ) {
        setError( error, QStringLiteral( "failed to create storage directory: %1" ).arg( root ) );
        return false;
    }

    QSaveFile manifest{ QDir{ root }.filePath( QStringLiteral( "storage-manifest.ini" ) ) };
    const QByteArray contents{ "[Storage]\nlayoutVersion=1\nproduct=ZzLogg\n" };
    if ( !manifest.open( QIODevice::WriteOnly ) || manifest.write( contents ) != contents.size()
         || !manifest.commit() ) {
        setError( error, QStringLiteral( "failed to write storage manifest: %1" ).arg( root ) );
        return false;
    }
    return true;
}

bool StorageValidator::hasCompatibleManifest( const QString& root )
{
    const QString normalizedRoot = normalizedAbsolutePath( root );
    if ( normalizedRoot.isEmpty() ) {
        return false;
    }
    const QString manifestPath = QDir{ normalizedRoot }.filePath( QStringLiteral( "storage-manifest.ini" ) );
    if ( !QFileInfo{ manifestPath }.isFile() ) {
        return false;
    }
    QSettings manifest{ manifestPath, QSettings::IniFormat };
    return manifest.status() == QSettings::NoError
           && manifest.value( QStringLiteral( "Storage/layoutVersion" ) ).toInt() == 1
           && manifest.value( QStringLiteral( "Storage/product" ) ).toString()
                  == QStringLiteral( "ZzLogg" );
}
