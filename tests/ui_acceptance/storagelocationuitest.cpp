#include "storagebootstrapdialog.h"
#include "storagelocationpage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QString normalized( const QString& path )
{
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

template <typename T>
T* child( QWidget& widget, const char* objectName )
{
    auto* result = widget.findChild<T*>( QString::fromLatin1( objectName ) );
    Q_ASSERT( result != nullptr );
    return result;
}

void assertNoStorageFiles( const QString& root )
{
    const QStringList forbiddenNames{ QStringLiteral( "ZzLogg.storage.ini" ),
                                      QStringLiteral( "storage-manifest.ini" ),
                                      QStringLiteral( "ZzLogg.ini" ),
                                      QStringLiteral( "ZzLogg_session.ini" ) };
    for ( const QString& fileName : forbiddenNames ) {
        QVERIFY2( !QFile::exists( QDir{ root }.filePath( fileName ) ),
                  qPrintable( QDir{ root }.filePath( fileName ) ) );
    }

    QDirIterator entries{ root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories };
    while ( entries.hasNext() ) {
        const QString entryPath = entries.next();
        QVERIFY2( false, qPrintable( entryPath ) );
    }
}

} // namespace

class StorageLocationUiTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void displaysAndRoundTripsEveryStorageMode();
    void customValidationControlsContinueAndReportsOnlyValidityChanges();
    void commandLineManagedLocationCannotBeEdited();
    void dialogReturnsSelectionOnlyAfterAcceptedAndCancelCreatesNoBusinessFiles();
};

void StorageLocationUiTest::displaysAndRoundTripsEveryStorageMode()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory
        = temporaryDirectory.filePath( QStringLiteral( "application" ) );
    const QString userDataDirectory = temporaryDirectory.filePath( QStringLiteral( "user-data" ) );
    const QString customDirectory
        = temporaryDirectory.filePath( QStringLiteral( "custom/../custom-root" ) );
    QVERIFY( QDir{}.mkpath( applicationDirectory ) );

    StorageLocationPage page;
    page.setApplicationDirectory( applicationDirectory );
    page.setUserDataDirectory( userDataDirectory );

    child<QRadioButton>( page, "userStorageRadio" )->click();
    QCOMPARE( page.location().mode, StorageMode::UserDirectory );
    QCOMPARE( page.location().dataRoot, normalized( userDataDirectory ) );

    child<QRadioButton>( page, "programStorageRadio" )->click();
    QCOMPARE( page.location().mode, StorageMode::ProgramDirectory );
    QCOMPARE( page.location().dataRoot, normalized( applicationDirectory + "/data" ) );
    QCOMPARE( child<QLineEdit>( page, "storagePathPreview" )->text(),
              normalized( applicationDirectory + "/data" ) );
    QVERIFY2( page.isSelectionValid(), qPrintable( page.validationError() ) );
    QCOMPARE( QDir{ applicationDirectory }.entryList( { ".zzlogg-locator-probe-*" }, QDir::Files ),
              QStringList{} );
    assertNoStorageFiles( applicationDirectory );
    assertNoStorageFiles( QDir{ applicationDirectory }.filePath( "data" ) );
    assertNoStorageFiles( userDataDirectory );

    page.setLocation( { StorageMode::UserDirectory, userDataDirectory, {}, false } );
    QCOMPARE( child<QRadioButton>( page, "userStorageRadio" )->isChecked(), true );
    QCOMPARE( page.location().dataRoot, normalized( userDataDirectory ) );

    page.setLocation( { StorageMode::ProgramDirectory, {}, {}, false } );
    QCOMPARE( child<QRadioButton>( page, "programStorageRadio" )->isChecked(), true );
    QCOMPARE( page.location().dataRoot, normalized( applicationDirectory + "/data" ) );

    page.setLocation( { StorageMode::CustomDirectory, customDirectory, {}, false } );
    QCOMPARE( child<QRadioButton>( page, "customStorageRadio" )->isChecked(), true );
    QCOMPARE( child<QLineEdit>( page, "customStoragePath" )->text(),
              normalized( customDirectory ) );
    QCOMPARE( page.location().mode, StorageMode::CustomDirectory );
    QCOMPARE( page.location().dataRoot, normalized( customDirectory ) );
    QVERIFY2( page.isSelectionValid(), qPrintable( page.validationError() ) );
}

void StorageLocationUiTest::customValidationControlsContinueAndReportsOnlyValidityChanges()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );

    StorageBootstrapDialog dialog;
    dialog.configurePaths( temporaryDirectory.filePath( QStringLiteral( "application" ) ),
                           temporaryDirectory.filePath( QStringLiteral( "user-data" ) ) );
    auto* page = child<StorageLocationPage>( dialog, "storageLocationPage" );
    auto* custom = child<QRadioButton>( *page, "customStorageRadio" );
    auto* path = child<QLineEdit>( *page, "customStoragePath" );
    auto* error = child<QLabel>( *page, "storageValidationLabel" );
    auto* continueButton = child<QPushButton>( dialog, "storageContinueButton" );
    QSignalSpy validitySpy{ page, &StorageLocationPage::validityChanged };

    custom->click();
    path->setText( QStringLiteral( "relative-storage" ) );
    QVERIFY( !page->isSelectionValid() );
    QVERIFY( !continueButton->isEnabled() );
    QVERIFY( !error->text().isEmpty() );
    const int invalidSignalCount = validitySpy.count();
    path->setText( QStringLiteral( "still-relative" ) );
    QCOMPARE( validitySpy.count(), invalidSignalCount );

    const QString validRoot = temporaryDirectory.filePath( QStringLiteral( "valid-custom" ) );
    path->setText( validRoot );
    QVERIFY2( page->isSelectionValid(), qPrintable( page->validationError() ) );
    QVERIFY( continueButton->isEnabled() );
    QCOMPARE( validitySpy.count(), invalidSignalCount + 1 );
}

void StorageLocationUiTest::commandLineManagedLocationCannotBeEdited()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString customRoot = temporaryDirectory.filePath( QStringLiteral( "command-line-root" ) );

    StorageLocationPage page;
    page.setApplicationDirectory( temporaryDirectory.filePath( QStringLiteral( "application" ) ) );
    page.setUserDataDirectory( temporaryDirectory.filePath( QStringLiteral( "user-data" ) ) );
    page.setLocation( { StorageMode::CustomDirectory, customRoot, {}, true } );

    QVERIFY( !child<QRadioButton>( page, "userStorageRadio" )->isEnabled() );
    QVERIFY( !child<QRadioButton>( page, "programStorageRadio" )->isEnabled() );
    QVERIFY( !child<QRadioButton>( page, "customStorageRadio" )->isEnabled() );
    QVERIFY( !child<QLineEdit>( page, "customStoragePath" )->isEnabled() );
    QVERIFY( !child<QPushButton>( page, "browseStorageButton" )->isEnabled() );
    QCOMPARE( page.location().mode, StorageMode::CustomDirectory );
    QCOMPARE( page.location().dataRoot, normalized( customRoot ) );
    QVERIFY( page.location().commandLineOverride );
}

void StorageLocationUiTest::dialogReturnsSelectionOnlyAfterAcceptedAndCancelCreatesNoBusinessFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory
        = temporaryDirectory.filePath( QStringLiteral( "application" ) );
    const QString userDataDirectory = temporaryDirectory.filePath( QStringLiteral( "user-data" ) );
    const QString customRoot = temporaryDirectory.filePath( QStringLiteral( "custom" ) );
    QVERIFY( QDir{}.mkpath( applicationDirectory ) );

    StorageBootstrapDialog cancelled;
    cancelled.configurePaths( applicationDirectory, userDataDirectory );
    QVERIFY( !cancelled.selectedLocation().has_value() );
    child<QRadioButton>( *child<StorageLocationPage>( cancelled, "storageLocationPage" ),
                         "programStorageRadio" )
        ->click();
    child<QPushButton>( cancelled, "storageCancelButton" )->click();
    QCOMPARE( cancelled.result(), QDialog::Rejected );
    QVERIFY( !cancelled.selectedLocation().has_value() );
    assertNoStorageFiles( applicationDirectory );
    assertNoStorageFiles( QDir{ applicationDirectory }.filePath( "data" ) );
    assertNoStorageFiles( userDataDirectory );

    StorageBootstrapDialog accepted;
    accepted.configurePaths( applicationDirectory, userDataDirectory );
    auto* page = child<StorageLocationPage>( accepted, "storageLocationPage" );
    child<QRadioButton>( *page, "customStorageRadio" )->click();
    child<QLineEdit>( *page, "customStoragePath" )->setText( customRoot );
    auto* continueButton = child<QPushButton>( accepted, "storageContinueButton" );
    QVERIFY( continueButton->isEnabled() );
    continueButton->click();
    QCOMPARE( accepted.result(), QDialog::Accepted );
    QVERIFY( accepted.selectedLocation().has_value() );
    QCOMPARE( accepted.selectedLocation()->dataRoot, normalized( customRoot ) );
    QVERIFY( !QFile::exists( QDir{ applicationDirectory }.filePath( "ZzLogg.storage.ini" ) ) );
    QVERIFY( !QFile::exists( QDir{ customRoot }.filePath( "storage-manifest.ini" ) ) );
    QCOMPARE( QDir{ customRoot }.entryList( { ".zzlogg-*" }, QDir::Files ), QStringList{} );
}

QTEST_MAIN( StorageLocationUiTest )
#include "storagelocationuitest.moc"
