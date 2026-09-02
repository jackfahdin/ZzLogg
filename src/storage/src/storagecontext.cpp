#include "storagecontext.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>

#include <utility>

namespace {

QString normalizedPath( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

QMutex& contextMutex()
{
    static QMutex mutex;
    return mutex;
}

StorageContext*& installedContext()
{
    static StorageContext* context = nullptr;
    return context;
}

bool ensureDirectory( const QString& path, QString* error )
{
    if ( QDir().mkpath( path ) ) {
        return true;
    }
    if ( error != nullptr ) {
        *error = QStringLiteral( "failed to create directory: %1" ).arg( path );
    }
    return false;
}

} // namespace

StorageContext::StorageContext( StorageLocation location, StorageRuntimePaths runtimePaths )
    : location_( std::move( location ) )
    , runtimePaths_( std::move( runtimePaths ) )
{
    location_.dataRoot = normalizedPath( location_.dataRoot );
    location_.locatorPath = normalizedPath( location_.locatorPath );
    runtimePaths_.applicationDirectory = normalizedPath( runtimePaths_.applicationDirectory );
    runtimePaths_.appConfigDirectory = normalizedPath( runtimePaths_.appConfigDirectory );
    runtimePaths_.userDataDirectory = normalizedPath( runtimePaths_.userDataDirectory );
}

bool StorageContext::install( StorageLocation location, QString* error )
{
    return install( std::move( location ), {}, error );
}

bool StorageContext::install( StorageLocation location, StorageRuntimePaths runtimePaths,
                              QString* error )
{
    const QMutexLocker locker{ &contextMutex() };
    if ( installedContext() != nullptr ) {
        if ( error != nullptr ) {
            *error = QStringLiteral( "storage context already installed" );
        }
        return false;
    }
    installedContext()
        = new StorageContext( std::move( location ), std::move( runtimePaths ) );
    return true;
}

bool StorageContext::isInstalled()
{
    const QMutexLocker locker{ &contextMutex() };
    return installedContext() != nullptr;
}

const StorageContext& StorageContext::current()
{
    const QMutexLocker locker{ &contextMutex() };
    Q_ASSERT( installedContext() != nullptr );
    return *installedContext();
}

const StorageLocation& StorageContext::location() const
{
    return location_;
}

const StorageRuntimePaths& StorageContext::runtimePaths() const
{
    return runtimePaths_;
}

QString StorageContext::dataRoot() const
{
    return location_.dataRoot;
}

QString StorageContext::configDirectory() const
{
    return QDir( dataRoot() ).filePath( QStringLiteral( "config" ) );
}

QString StorageContext::sessionDirectory() const
{
    return QDir( dataRoot() ).filePath( QStringLiteral( "session" ) );
}

QString StorageContext::logsDirectory() const
{
    return QDir( dataRoot() ).filePath( QStringLiteral( "logs" ) );
}

QString StorageContext::crashesDirectory() const
{
    return QDir( dataRoot() ).filePath( QStringLiteral( "crashes" ) );
}

QString StorageContext::configFilePath() const
{
    return QDir( configDirectory() ).filePath( QStringLiteral( "ZzLogg.ini" ) );
}

QString StorageContext::sessionFilePath() const
{
    return QDir( sessionDirectory() ).filePath( QStringLiteral( "ZzLogg_session.ini" ) );
}

QString StorageContext::manifestFilePath() const
{
    return QDir( dataRoot() ).filePath( QStringLiteral( "storage-manifest.ini" ) );
}

bool StorageContext::ensureDirectories( QString* error ) const
{
    return ensureDirectory( dataRoot(), error ) && ensureDirectory( configDirectory(), error )
           && ensureDirectory( sessionDirectory(), error )
           && ensureDirectory( logsDirectory(), error )
           && ensureDirectory( crashesDirectory(), error );
}
