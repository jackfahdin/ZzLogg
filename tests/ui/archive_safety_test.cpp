#include "decompressor.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>
#include <karchive.h>
#include <ktar.h>
#include <limits>

namespace {
QString fixture( const QString& name )
{
    return QString::fromUtf8( SAFETY_FIXTURES ) + '/' + name;
}
QByteArray contents( const QString& path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return {};
    return f.readAll();
}
void put( const QString& path, const QByteArray& data )
{
    QVERIFY( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile f( path );
    QVERIFY( f.open( QIODevice::WriteOnly ) );
    QCOMPARE( f.write( data ), data.size() );
}
void extract( const QString& input, const QString& dest, bool expected, bool cancelled = false )
{
    AtomicFlag cancel( cancelled );
    Decompressor worker;
    QSignalSpy done( &worker, &Decompressor::finished );
    QVERIFY( worker.extract( input, dest, cancel ) );
    const bool timely = !done.isEmpty() || done.wait( 2000 );
    if ( !timely )
        cancel.set();
    const bool result = worker.waitForResult();
    QCoreApplication::processEvents();
    QVERIFY( timely );
    QCOMPARE( result, expected );
    QCOMPARE( done.size(), qsizetype( 1 ) );
    QCOMPARE( done.front().front().toBool(), expected );
}
} // namespace
class ArchiveSafetyTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void hostileArchive_data()
    {
        QTest::addColumn<QString>( "file" );
        for ( const auto* name :
              { "parent", "prefix-sibling", "backslash", "external-link", "indirect-escape",
                "case-link-escape", "unicode-link-escape", "case-collision", "unicode-collision" } )
            for ( const auto* suffix : { "tar", "zip" } ) {
                const auto file = QString( name ) + '.' + suffix;
                QTest::newRow( qPrintable( file ) ) << file;
            }
    }
    void hostileArchive()
    {
        QFETCH( QString, file );
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        put( temp.filePath( "outside/marker.txt" ), "keep" );
        put( temp.filePath( "result-evil/marker.txt" ), "keep" );
        const auto dest = temp.filePath( "result" );
        extract( fixture( file ), dest, false );
        QCOMPARE( contents( temp.filePath( "outside/marker.txt" ) ), QByteArray( "keep" ) );
        QCOMPARE( contents( temp.filePath( "result-evil/marker.txt" ) ), QByteArray( "keep" ) );
        QVERIFY( !QFileInfo::exists( dest ) ); // Full preflight rejects before any output.
    }
    void internalLinks_data()
    {
        QTest::addColumn<QString>( "suffix" );
        QTest::newRow( "tar" ) << QString( "tar" );
        QTest::newRow( "zip" ) << QString( "zip" );
    }
    void internalLinks()
    {
        QFETCH( QString, suffix );
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
#ifdef Q_OS_WIN
        QSKIP( "Native Windows symlink semantics require a dedicated platform run" );
#endif
        const auto dest = temp.filePath( "result" );
        extract( fixture( "internal-links." + suffix ), dest, true );
        for ( const auto* file :
              { "logs/value.log", "alias.log", "nested/link.log", "via-dir.log" } )
            QCOMPARE( contents( dest + '/' + file ), QByteArray( "value\n" ) );
        QVERIFY( QFileInfo( dest + "/alias.log" ).isSymLink() );
    }
    void existingLinks_data()
    {
        QTest::addColumn<QString>( "kind" );
        for ( const auto* kind : { "file", "directory", "root", "dangling" } )
            QTest::newRow( kind ) << QString( kind );
    }
    void existingLinks()
    {
        QFETCH( QString, kind );
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
#ifdef Q_OS_WIN
        QSKIP( "QFile::link creates shortcuts on Windows; needs native symlink fixture" );
#endif
        const auto dest = temp.filePath( "result" );
        put( temp.filePath( "outside/marker.txt" ), "keep" );
        QVERIFY( QDir().mkpath( dest + "/logs" ) );
        QString target = temp.filePath( "outside/marker.txt" ), link = dest + "/logs/value.log";
        if ( kind == "directory" ) {
            QVERIFY( QDir( dest + "/logs" ).removeRecursively() );
            target = temp.filePath( "outside" );
            link = dest + "/logs";
        }
        else if ( kind == "root" ) {
            QVERIFY( QDir( dest ).removeRecursively() );
            target = temp.filePath( "outside" );
            link = dest;
        }
        else if ( kind == "dangling" )
            target = temp.filePath( "outside/missing.log" );
        QVERIFY( QFile::link( target, link ) );
        extract( fixture( "internal-links.tar" ), dest, false );
        QCOMPARE( contents( temp.filePath( "outside/marker.txt" ) ), QByteArray( "keep" ) );
        QVERIFY( !QFileInfo::exists( temp.filePath( "outside/missing.log" ) ) );
        QVERIFY( !QFileInfo::exists( temp.filePath( "outside/value.log" ) ) );
        QVERIFY( QFileInfo( link ).isSymLink() );
    }
    void preservesHardlinkTarget()
    {
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        const auto dest = temp.filePath( "result" );
        put( temp.filePath( "outside.log" ), "keep" );
        QVERIFY( QDir().mkpath( dest + "/logs" ) );
        std::error_code error;
        std::filesystem::create_hard_link(
            std::filesystem::path( temp.filePath( "outside.log" ).toStdU16String() ),
            std::filesystem::path( ( dest + "/logs/value.log" ).toStdU16String() ), error );
        if ( error )
            QSKIP( "Hard links unavailable on temporary filesystem" );
        extract( fixture( "internal-links.tar" ), dest, true );
        QCOMPARE( contents( temp.filePath( "outside.log" ) ), QByteArray( "keep" ) );
        QCOMPARE( contents( dest + "/logs/value.log" ), QByteArray( "value\n" ) );
    }
    void invalidSize_data()
    {
        QTest::addColumn<QString>( "name" );
        QTest::newRow( "invalid" ) << QString( "invalid-size.tar" );
        QTest::newRow( "negative" ) << QString( "negative-size.tar" );
    }
    void invalidSize()
    {
        QFETCH( QString, name );
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        extract( fixture( name ), temp.filePath( "result" ), false );
        QVERIFY( !QFileInfo::exists( temp.filePath( "result/value.log" ) ) );
    }
    void failedCopyPreservesExistingFile()
    {
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        put( temp.filePath( "value.log" ), "keep" );
        QByteArray bytes( "short" );
        QBuffer data( &bytes );
        QVERIFY( data.open( QIODevice::ReadOnly ) );
        KTar archive( &data );
        KArchiveFile file( &archive, "value.log", 0644, QDateTime(), {}, {}, {}, 0, 100 );
        QAtomicInt cancelled( 0 );
        QVERIFY( !file.copyTo( temp.path(), cancelled ) );
        QCOMPARE( contents( temp.filePath( "value.log" ) ), QByteArray( "keep" ) );
        QCOMPARE(
            QDir( temp.path() ).entryList( QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot ),
            QStringList{ "value.log" } );
    }
    void cancelledHugeDeclaration()
    {
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        QBuffer data;
        KTar archive( &data );
        KArchiveDirectory root( &archive, "/", 0755, QDateTime(), {}, {}, {} );
        root.addEntry( new KArchiveFile( &archive, "huge.log", 0644, QDateTime(), {}, {}, {}, 0,
                                         std::numeric_limits<qint64>::max() ) );
        QAtomicInt cancelled( 1 );
        QVERIFY( !root.copyTo( temp.filePath( "result" ), cancelled ) );
        QVERIFY( !QFileInfo::exists( temp.filePath( "result" ) ) );
    }
    void negativeDeclaredSize()
    {
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        QBuffer data;
        KTar archive( &data );
        KArchiveDirectory root( &archive, "/", 0755, QDateTime(), {}, {}, {} );
        root.addEntry(
            new KArchiveFile( &archive, "value.log", 0644, QDateTime(), {}, {}, {}, 0, -1 ) );
        QAtomicInt cancelled( 0 );
        QVERIFY( !root.copyTo( temp.filePath( "result" ), cancelled ) );
        QVERIFY( !QFileInfo::exists( temp.filePath( "result" ) ) );
    }
    void totalSizeOverflow()
    {
        QTemporaryDir temp;
        QVERIFY( temp.isValid() );
        QBuffer data;
        KTar archive( &data );
        KArchiveDirectory root( &archive, "/", 0755, QDateTime(), {}, {}, {} );
        for ( const auto* name : { "a.log", "b.log" } )
            root.addEntry( new KArchiveFile( &archive, name, 0644, QDateTime(), {}, {}, {}, 0,
                                             std::numeric_limits<qint64>::max() ) );
        QAtomicInt cancelled( 0 );
        QVERIFY( !root.copyTo( temp.filePath( "result" ), cancelled ) );
        QVERIFY( !QFileInfo::exists( temp.filePath( "result" ) ) );
    }
};
QTEST_GUILESS_MAIN( ArchiveSafetyTest )
#include "archive_safety_test.moc"
