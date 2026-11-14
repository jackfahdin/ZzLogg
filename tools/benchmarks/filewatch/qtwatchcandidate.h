#pragma once

#include "signalthrottler.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <functional>
#include <utility>

// Experimental adapter. Never linked into the application.
class QtWatchCandidate final : public QObject {
public:
    QtWatchCandidate( bool native, bool polling, int interval,
                      std::function<void( const QString& )> changed, QObject* parent = nullptr )
        : QObject( parent )
        , native_( native )
        , polling_( polling )
        , changed_( std::move( changed ) )
    {
        throttler_.setTimeout( 250 );
        connect( &throttler_, &SignalThrottler::triggered, this, [ this ] {
            const auto pending = std::exchange( pending_, {} );
            for ( const auto& path : pending )
                if ( files_.contains( path ) )
                    changed_( path );
        } );
        connect( &nativeWatch_, &QFileSystemWatcher::fileChanged, this,
                 [ this ]( const QString& path ) {
                     queue( path );
                     registerPaths();
                 } );
        connect( &nativeWatch_, &QFileSystemWatcher::directoryChanged, this,
                 [ this ]( const QString& dir ) {
                     for ( auto it = files_.cbegin(); it != files_.cend(); ++it )
                         if ( QFileInfo( it.key() ).absolutePath() == dir )
                             queue( it.key() );
                     registerPaths();
                 } );
        connect( &timer_, &QTimer::timeout, this, [ this ] {
            for ( auto it = files_.begin(); it != files_.end(); ++it ) {
                const auto snapshot = read( it.key() );
                // Poll all files only when requested, otherwise cover failed native registrations.
                if ( ( polling_ || unregistered_.contains( it.key() ) )
                     && !( snapshot == it.value() ) )
                    queue( it.key() );
                it.value() = snapshot;
            }
            registerPaths();
        } );
        timer_.setInterval( interval );
    }

    ~QtWatchCandidate() override
    {
        // SignalThrottler flushes during destruction; do not invoke a caller after teardown.
        disconnect( &throttler_, nullptr, this, nullptr );
    }

    void addFile( const QString& path )
    {
        if ( files_.contains( path ) )
            return;
        files_.insert( path, read( path ) );
        registerPaths();
    }
    void removeFile( const QString& path )
    {
        files_.remove( path );
        pending_.remove( path );
        registerPaths();
    }
    qsizetype nativeRegistrations() const
    {
        return nativeWatch_.files().size() + nativeWatch_.directories().size();
    }

private:
    struct Snapshot {
        qint64 size;
        qint64 modified;
        bool operator==( const Snapshot& other ) const
        {
            return size == other.size && modified == other.modified;
        }
    };
    static Snapshot read( const QString& path )
    {
        const QFileInfo info( path );
        return { info.size(), info.lastModified().toMSecsSinceEpoch() };
    }
    void queue( const QString& path )
    {
        if ( !files_.contains( path ) )
            return;
        pending_.insert( path );
        throttler_.throttle();
    }
    void registerPaths()
    {
        QSet<QString> wanted;
        if ( native_ ) {
            for ( auto it = files_.cbegin(); it != files_.cend(); ++it ) {
                wanted.insert( it.key() );
                wanted.insert( QFileInfo( it.key() ).absolutePath() );
            }
        }
        auto current = nativeWatch_.files() + nativeWatch_.directories();
        for ( const auto& path : current )
            if ( !wanted.contains( path ) )
                nativeWatch_.removePath( path );
        current = nativeWatch_.files() + nativeWatch_.directories();
        for ( const auto& path : wanted )
            if ( !current.contains( path ) && QFileInfo::exists( path ) )
                nativeWatch_.addPath( path );
        unregistered_.clear();
        if ( native_ )
            for ( auto it = files_.cbegin(); it != files_.cend(); ++it )
                if ( !nativeWatch_.files().contains( it.key() ) )
                    unregistered_.insert( it.key() );
        if ( !files_.isEmpty() && ( polling_ || !unregistered_.isEmpty() ) ) {
            if ( !timer_.isActive() )
                timer_.start();
        }
        else
            timer_.stop();
    }
    bool native_;
    bool polling_;
    std::function<void( const QString& )> changed_;
    QMap<QString, Snapshot> files_;
    QSet<QString> pending_;
    QSet<QString> unregistered_;
    QFileSystemWatcher nativeWatch_;
    QTimer timer_;
    SignalThrottler throttler_;
};
