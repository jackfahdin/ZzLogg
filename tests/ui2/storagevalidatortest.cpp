#include "storagevalidator.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

QByteArray readBytes( const QString& path )
{
    QFile file{ path };
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return file.readAll();
}

} // namespace

class StorageValidatorTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void rejectsEmptyRelativeAndFileRoots();
    void acceptsWritableEmptyDirectoryAndCleansProbeFiles();
    void preservesExistingProbeNamedFileAndRejectsDirectory();
    void rejectsUnmanagedNonEmptyDirectory();
    void acceptsCompatibleManifestWhenAllowed();
    void writesCompatibleManifest();
};

void StorageValidatorTest::rejectsEmptyRelativeAndFileRoots()
{
    const auto empty = StorageValidator::validate( {}, false );
    QVERIFY( !empty.valid );

    const auto relative = StorageValidator::validate( QStringLiteral( "relative-storage" ), false );
    QVERIFY( !relative.valid );

    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString fileRoot = temporaryDirectory.filePath( QStringLiteral( "not-a-directory" ) );
    QFile file{ fileRoot };
    QVERIFY( file.open( QIODevice::WriteOnly ) );
    file.write( "file" );
    file.close();

    const auto result = StorageValidator::validate( fileRoot, false );
    QVERIFY( !result.valid );
    QVERIFY( result.error.contains( QDir::cleanPath( fileRoot ) ) );
}

void StorageValidatorTest::acceptsWritableEmptyDirectoryAndCleansProbeFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString root = temporaryDirectory.filePath( QStringLiteral( "empty-root" ) );

    const auto result = StorageValidator::validate( root, false );

    QVERIFY2( result.valid, qPrintable( result.error ) );
    QVERIFY( !result.managedDirectory );
    QCOMPARE( result.normalizedRoot, QDir::cleanPath( root ) );
    QVERIFY( QDir{ root }.exists() );
    QCOMPARE( QDir{ root }.entryList( { QStringLiteral( ".zzlogg-write-test-*" ) }, QDir::Files ),
              QStringList{} );
}

void StorageValidatorTest::preservesExistingProbeNamedFileAndRejectsDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString root = temporaryDirectory.filePath( QStringLiteral( "occupied-root" ) );
    QVERIFY( QDir{}.mkpath( root ) );
    const QString existingProbe = QDir{ root }.filePath( QStringLiteral( ".zzlogg-write-test-user-file" ) );
    QFile file{ existingProbe };
    QVERIFY( file.open( QIODevice::WriteOnly ) );
    file.write( "user-owned" );
    file.close();

    const auto result = StorageValidator::validate( root, false );

    QVERIFY( !result.valid );
    QVERIFY( QFile::exists( existingProbe ) );
    QCOMPARE( readBytes( existingProbe ), QByteArray( "user-owned" ) );
}

void StorageValidatorTest::rejectsUnmanagedNonEmptyDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString root = temporaryDirectory.filePath( QStringLiteral( "occupied-root" ) );
    QVERIFY( QDir{}.mkpath( root ) );
    QFile unrelatedFile{ QDir{ root }.filePath( QStringLiteral( "unrelated.txt" ) ) };
    QVERIFY( unrelatedFile.open( QIODevice::WriteOnly ) );
    unrelatedFile.write( "keep" );
    unrelatedFile.close();

    const auto result = StorageValidator::validate( root, false );

    QVERIFY( !result.valid );
    QVERIFY( !result.managedDirectory );
    QVERIFY( result.error.contains( QDir::cleanPath( root ) ) );
}

void StorageValidatorTest::acceptsCompatibleManifestWhenAllowed()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString root = temporaryDirectory.filePath( QStringLiteral( "managed-root" ) );
    QVERIFY( QDir{}.mkpath( root ) );
    QSettings manifest{ QDir{ root }.filePath( QStringLiteral( "storage-manifest.ini" ) ),
                        QSettings::IniFormat };
    manifest.setValue( QStringLiteral( "Storage/layoutVersion" ), 1 );
    manifest.setValue( QStringLiteral( "Storage/product" ), QStringLiteral( "ZzLogg" ) );
    manifest.sync();
    QCOMPARE( manifest.status(), QSettings::NoError );

    const auto rejected = StorageValidator::validate( root, false );
    QVERIFY( !rejected.valid );

    const auto accepted = StorageValidator::validate( root, true );
    QVERIFY2( accepted.valid, qPrintable( accepted.error ) );
    QVERIFY( accepted.managedDirectory );
    QVERIFY( StorageValidator::hasCompatibleManifest( root ) );
}

void StorageValidatorTest::writesCompatibleManifest()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString root = temporaryDirectory.filePath( QStringLiteral( "manifest-root" ) );
    QVERIFY( QDir{}.mkpath( root ) );
    const StorageContext context{ { StorageMode::CustomDirectory, root, {}, false } };

    QString error;
    QVERIFY2( StorageValidator::writeManifest( context, &error ), qPrintable( error ) );

    QSettings manifest{ context.manifestFilePath(), QSettings::IniFormat };
    QCOMPARE( manifest.value( QStringLiteral( "Storage/layoutVersion" ) ).toInt(), 1 );
    QCOMPARE( manifest.value( QStringLiteral( "Storage/product" ) ).toString(),
              QStringLiteral( "ZzLogg" ) );
    QVERIFY( StorageValidator::hasCompatibleManifest( root ) );
}

QTEST_MAIN( StorageValidatorTest )
#include "storagevalidatortest.moc"
