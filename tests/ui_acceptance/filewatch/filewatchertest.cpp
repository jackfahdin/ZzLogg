#include "filewatcher.h"
#include "configuration.h"
#include "storagecontext.h"
#ifndef ZZLOGG_WATCH_PROBE
#include "logdata.h"
#include <QSignalSpy>
#endif
#ifdef ZZLOGG_WATCH_PROBE
#include "qtwatchcandidate.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#endif

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <ctime>
#include <functional>
#include <memory>

namespace {
bool writeFile( const QString& path, const QByteArray& bytes, bool append = false )
{
    QFile file( path );
    return file.open( QIODevice::WriteOnly | ( append ? QIODevice::Append : QIODevice::Truncate ) )
           && file.write( bytes ) == bytes.size() && file.flush();
}
QString scratchTemplate()
{
    const auto root = qEnvironmentVariable( "ZZLOGG_WATCH_TEST_ROOT", QDir::tempPath() );
    return QDir( root ).filePath( "zzlogg-watch-XXXXXX" );
}

class Monitor {
public:
    struct Observation {
        bool exists;
        QByteArray bytes;
    };
    Monitor( bool native, bool poll )
    {
        const auto callback = [ this ]( const QString& path ) {
            QFile file( path );
            const bool exists = file.exists();
            QByteArray bytes;
            if ( file.open( QIODevice::ReadOnly ) )
                bytes = file.readAll();
            observations.insert( path, { exists, bytes } );
            ++notifications;
        };
#ifdef ZZLOGG_WATCH_PROBE
        if ( qEnvironmentVariable( "ZZLOGG_WATCH_BACKEND" ) == "qt" ) {
            candidate_ = std::make_unique<QtWatchCandidate>( native, poll, 100, callback );
            return;
        }
#endif
        auto& config = Configuration::getSynced();
        config.setNativeFileWatchEnabled( native );
        config.setPollingEnabled( poll );
        config.setPollIntervalMs( 100 );
        auto& watcher = FileWatcher::getFileWatcher();
        watcher.updateConfiguration();
        connection_ = QObject::connect( &watcher, &FileWatcher::fileChanged, &context_, callback );
    }
    ~Monitor()
    {
        QObject::disconnect( connection_ );
        const auto paths = paths_;
        for ( const auto& path : paths )
            remove( path );
    }
    void add( const QString& path )
    {
        paths_.insert( path );
#ifdef ZZLOGG_WATCH_PROBE
        if ( candidate_ ) {
            candidate_->addFile( path );
            return;
        }
#endif
        FileWatcher::getFileWatcher().addFile( path );
    }
    void remove( const QString& path )
    {
        paths_.remove( path );
#ifdef ZZLOGG_WATCH_PROBE
        if ( candidate_ ) {
            candidate_->removeFile( path );
            return;
        }
#endif
        FileWatcher::getFileWatcher().removeFile( path );
    }
    bool saw( const QString& path, const QByteArray& bytes ) const
    {
        const auto it = observations.constFind( path );
        return it != observations.cend() && it->exists && it->bytes == bytes;
    }
    bool sawDeletion( const QString& path ) const
    {
        const auto it = observations.constFind( path );
        return it != observations.cend() && !it->exists;
    }
    QMap<QString, Observation> observations;
    int notifications = 0;

private:
    QObject context_;
    QMetaObject::Connection connection_;
    QSet<QString> paths_;
#ifdef ZZLOGG_WATCH_PROBE
    std::unique_ptr<QtWatchCandidate> candidate_;
#endif
};

#ifdef ZZLOGG_WATCH_PROBE
// MSVC std::clock measures wall time, so use process kernel+user time on Windows.
double processCpuMs()
{
#ifdef Q_OS_WIN
    FILETIME creation, exit, kernel, user;
    if ( !GetProcessTimes( GetCurrentProcess(), &creation, &exit, &kernel, &user ) )
        return -1;
    ULARGE_INTEGER kernelTicks{}, userTicks{};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    return static_cast<double>( kernelTicks.QuadPart + userTicks.QuadPart ) / 10000.0;
#else
    const auto ticks = std::clock();
    return ticks == std::clock_t{ -1 } ? -1
                                       : 1000.0 * static_cast<double>( ticks ) / CLOCKS_PER_SEC;
#endif
}
#endif

void modes()
{
    QTest::addColumn<bool>( "native" );
    QTest::addColumn<bool>( "poll" );
    QTest::newRow( "native" ) << true << false;
    QTest::newRow( "poll" ) << false << true;
    QTest::newRow( "hybrid" ) << true << true;
}
} // namespace

class FileWatcherTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void lifecycle_data()
    {
        modes();
    }
    void lifecycle()
    {
        QFETCH( bool, native );
        QFETCH( bool, poll );
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto path = directory.filePath( QStringLiteral( "日志 with spaces.log" ) );
        QVERIFY( writeFile( path, "first\n" ) );
        Monitor monitor( native, poll );
        monitor.add( path );

        QVERIFY( writeFile( path, "second\n", true ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "first\nsecond\n" ), 5000 );
        QVERIFY( writeFile( path, "short\n" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "short\n" ), 5000 );

        const auto oldModified = QFileInfo( path ).lastModified();
        QVERIFY( writeFile( path, "equal\n" ) );
        QFile rewritten( path );
        QVERIFY( rewritten.open( QIODevice::ReadWrite ) );
        QVERIFY(
            rewritten.setFileTime( oldModified.addSecs( 2 ), QFileDevice::FileModificationTime ) );
        rewritten.close();
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "equal\n" ), 5000 );

        QVERIFY( QFile::rename( path, path + ".1" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.sawDeletion( path ), 5000 );
        QVERIFY( writeFile( path, "rotated\n" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "rotated\n" ), 5000 );
        QVERIFY( writeFile( path, "continued\n", true ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "rotated\ncontinued\n" ), 5000 );

        QVERIFY( QFile::remove( path ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.sawDeletion( path ), 5000 );
        QVERIFY( writeFile( path, "recreated\n" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "recreated\n" ), 5000 );

        QSaveFile replacement( path );
        QVERIFY( replacement.open( QIODevice::WriteOnly ) );
        QCOMPARE( replacement.write( "atomic replacement\n" ), qint64{ 19 } );
        QVERIFY( replacement.commit() );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "atomic replacement\n" ), 5000 );
        QVERIFY( writeFile( path, "tail\n", true ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "atomic replacement\ntail\n" ), 5000 );
    }

    void duplicateRegistrationAndRemoval()
    {
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto path = directory.filePath( "duplicate.log" );
        QVERIFY( writeFile( path, "before" ) );
        Monitor monitor( true, true );
        monitor.add( path );
        monitor.add( path );
        QVERIFY( writeFile( path, "after" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "after" ), 5000 );
        // Drain notifications already queued before removal; the API does not cancel them.
        QTest::qWait( 600 );
        monitor.remove( path );
        monitor.observations.clear();
        QVERIFY( writeFile( path, "removed" ) );
        QTest::qWait( 600 );
        QVERIFY( !monitor.observations.contains( path ) );
    }

    void nativeDetectsPreservedMetadata_data()
    {
        QTest::addColumn<bool>( "poll" );
        QTest::newRow( "native" ) << false;
        QTest::newRow( "hybrid" ) << true;
    }
    void nativeDetectsPreservedMetadata()
    {
        QFETCH( bool, poll );
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto path = directory.filePath( "native-metadata.log" );
        QVERIFY( writeFile( path, "before" ) );
        const auto modified = QFileInfo( path ).lastModified();
        Monitor monitor( true, poll );
        monitor.add( path );
        QVERIFY( writeFile( path, "after!" ) );
        QFile file( path );
        QVERIFY( file.open( QIODevice::ReadWrite ) );
        QVERIFY( file.setFileTime( modified, QFileDevice::FileModificationTime ) );
        file.close();
        QCOMPARE( QFileInfo( path ).lastModified().toMSecsSinceEpoch(),
                  modified.toMSecsSinceEpoch() );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( path, "after!" ), 5000 );
    }

    void pollingMetadataBlindSpot()
    {
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto path = directory.filePath( "metadata.log" );
        QVERIFY( writeFile( path, "before" ) );
        const auto modified = QFileInfo( path ).lastModified();
        Monitor monitor( false, true );
        monitor.add( path );
        QVERIFY( writeFile( path, "after!" ) );
        QFile file( path );
        QVERIFY( file.open( QIODevice::ReadWrite ) );
        QVERIFY( file.setFileTime( modified, QFileDevice::FileModificationTime ) );
        file.close();
        QCOMPARE( QFileInfo( path ).lastModified().toMSecsSinceEpoch(),
                  modified.toMSecsSinceEpoch() );
        QTest::qWait( 600 );
        // Characterize the existing size/mtime-only polling limit, not content correctness.
        QVERIFY( !monitor.observations.contains( path ) );
    }

#ifndef ZZLOGG_WATCH_PROBE
    void indexedContentLifecycle_data()
    {
        modes();
    }
    void indexedContentLifecycle()
    {
        QFETCH( bool, native );
        QFETCH( bool, poll );
        auto& config = Configuration::getSynced();
        config.setNativeFileWatchEnabled( native );
        config.setPollingEnabled( poll );
        config.setPollIntervalMs( 100 );
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto path = directory.filePath( "indexed.log" );
        QVERIFY( writeFile( path, "first\n" ) );
        LogData data;
        QSignalSpy loaded( &data, &LogData::loadingFinished );
        data.attachFile( path );
        QTRY_VERIFY_WITH_TIMEOUT( !loaded.empty(), 5000 );
        QCOMPARE( data.getNbLine(), 1_lcount );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "first" ) );
        QVERIFY( writeFile( path, "second\n", true ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 2_lcount, 5000 );
        QCOMPARE( data.getLineString( 1_lnum ), QStringLiteral( "second" ) );
        QVERIFY( writeFile( path, "short\n" ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 1_lcount, 5000 );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "short" ) );

        const auto modified = QFileInfo( path ).lastModified();
        QVERIFY( writeFile( path, "eq\nal\n" ) );
        QFile file( path );
        QVERIFY( file.open( QIODevice::ReadWrite ) );
        QVERIFY( file.setFileTime( modified.addSecs( 2 ), QFileDevice::FileModificationTime ) );
        file.close();
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 2_lcount, 5000 );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "eq" ) );
        QCOMPARE( data.getLineString( 1_lnum ), QStringLiteral( "al" ) );

        QVERIFY( QFile::rename( path, path + ".1" ) );
        QVERIFY( writeFile( path, "rotated\nnew\nthird\n" ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 3_lcount, 5000 );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "rotated" ) );
        QVERIFY( QFile::remove( path ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 0_lcount, 5000 );
        QVERIFY( writeFile( path, "reborn\n" ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 1_lcount, 5000 );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "reborn" ) );
        QSaveFile replacement( path );
        QVERIFY( replacement.open( QIODevice::WriteOnly ) );
        QCOMPARE( replacement.write( "atomic\nview\n" ), qint64{ 12 } );
        QVERIFY( replacement.commit() );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 2_lcount, 5000 );
        QCOMPARE( data.getLineString( 0_lnum ), QStringLiteral( "atomic" ) );
        QVERIFY( writeFile( path, "tail\n", true ) );
        QTRY_COMPARE_WITH_TIMEOUT( data.getNbLine(), 3_lcount, 5000 );
        QCOMPARE( data.getLineString( 2_lnum ), QStringLiteral( "tail" ) );
    }
#endif

#ifdef ZZLOGG_WATCH_PROBE
    void recoveryReport_data()
    {
        modes();
    }
    void recoveryReport()
    {
        QFETCH( bool, native );
        QFETCH( bool, poll );
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        const auto child = directory.filePath( "parent" );
        const auto path = QDir( child ).filePath( "recover.log" );
        QVERIFY( QDir().mkpath( child ) );
        QVERIFY( writeFile( path, "before" ) );
        Monitor monitor( native, poll );
        monitor.add( path );
        QVERIFY( QDir( child ).removeRecursively() );
        QTest::qWait( 600 );
        QVERIFY( QDir().mkpath( child ) );
        QVERIFY( writeFile( path, "recreated parent" ) );
        const bool recovered
            = QTest::qWaitFor( [ & ] { return monitor.saw( path, "recreated parent" ); }, 3000 );
        qInfo().noquote() << "WATCH_RECOVERY" << QTest::currentDataTag() << "parent=" << recovered;
        monitor.remove( path );
        const auto missing = directory.filePath( "initially-missing/file.log" );
        monitor.add( missing );
        QVERIFY( QDir().mkpath( QFileInfo( missing ).absolutePath() ) );
        QVERIFY( writeFile( missing, "created later" ) );
        const bool registered
            = QTest::qWaitFor( [ & ] { return monitor.saw( missing, "created later" ); }, 3000 );
        qInfo().noquote() << "WATCH_RECOVERY" << QTest::currentDataTag()
                          << "initially-missing=" << registered;
        // Report failures as measurements; this exploratory test is not a replacement gate.
    }

    void metrics_data()
    {
        modes();
    }
    void metrics()
    {
        QFETCH( bool, native );
        QFETCH( bool, poll );
        QTemporaryDir directory( scratchTemplate() );
        QVERIFY( directory.isValid() );
        Monitor monitor( native, poll );
        for ( int i = 0; i < 128; ++i ) {
            const auto path = directory.filePath( QString::number( i ) + ".log" );
            QVERIFY( writeFile( path, "initial\n" ) );
            monitor.add( path );
        }
        QTest::qWait( 600 );
        const auto startCpu = processCpuMs();
        QElapsedTimer wall;
        wall.start();
        QTest::qWait( 2000 );
        const auto endCpu = processCpuMs();
        const auto cpuMs = startCpu < 0 || endCpu < 0 ? -1 : endCpu - startCpu;
        qInfo().noquote() << "WATCH_IDLE" << QTest::currentDataTag() << "files=128 cpu_ms=" << cpuMs
                          << "wall_ms=" << wall.elapsed();
        const auto active = directory.filePath( "0.log" );
        const int initialCount = monitor.notifications;
        int writes = 0;
        bool writeFailed = false;
        QTimer writer;
        connect( &writer, &QTimer::timeout, this, [ & ] {
            ++writes;
            writeFailed |= !writeFile( active, QByteArray::number( writes ) + '\n', true );
        } );
        writer.start( 20 );
        QTest::qWait( 2000 );
        writer.stop();
        QVERIFY( !writeFailed );
        const int during = monitor.notifications - initialCount;
        QElapsedTimer finalDelay;
        finalDelay.start();
        QVERIFY( writeFile( active, "final marker\n" ) );
        QTRY_VERIFY_WITH_TIMEOUT( monitor.saw( active, "final marker\n" ), 5000 );
        qInfo().noquote() << "WATCH_STREAM" << QTest::currentDataTag() << "writes=" << writes
                          << "notifications=" << during
                          << "final_delay_ms=" << finalDelay.elapsed();
        QVERIFY( during >= 2 ); // Continuous writes must not starve trailing notifications.
    }
#endif
};

int main( int argc, char** argv )
{
    QApplication app( argc, argv );
    QTemporaryDir settings;
    if ( !settings.isValid()
         || !StorageContext::install( { StorageMode::CustomDirectory, settings.path(),
                                        settings.filePath( "storage.ini" ), true } ) )
        return 1;
    Configuration::getSynced();
    FileWatcherTest test;
    return QTest::qExec( &test, argc, argv );
}
#include "filewatchertest.moc"
