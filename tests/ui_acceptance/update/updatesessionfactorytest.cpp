#include "applicationupdatesession.h"
#include "storagecontext.h"
#include "storagevalidator.h"

#include <QtTest>

class UpdateSessionFactoryTest : public QObject {
    Q_OBJECT
private slots:
    void restartReopensManagedStorage()
    {
        QTemporaryDir temporary;
        QVERIFY( temporary.isValid() );
        const auto locatorDirectory = temporary.filePath( "qt-config-locator" );
        QVERIFY( QDir().mkpath( locatorDirectory ) );
        QFile locator( QDir( locatorDirectory ).filePath( "storage.ini" ) );
        QVERIFY( locator.open( QIODevice::WriteOnly ) );
        locator.write( "existing storage locator" );
        locator.close();

        for ( const auto mode : { StorageMode::UserDirectory, StorageMode::ProgramDirectory,
                                  StorageMode::CustomDirectory } ) {
            const auto root = temporary.filePath( QString::number( static_cast<int>( mode ) ) );
            StorageContext storage( { mode, root, locator.fileName(), true },
                                    { temporary.path(), locatorDirectory, temporary.filePath( "user-data" ) } );
            QVERIFY( storage.ensureDirectories() );
            QVERIFY( StorageValidator::writeManifest( storage ) );
            QFile session( storage.sessionFilePath() );
            QVERIFY( session.open( QIODevice::WriteOnly ) );
            session.write( "existing session" );
            session.close();

            const auto restartRoot = updateRestartDataDirectory( storage );
            const auto validation = StorageValidator::validate( restartRoot, true );
            QVERIFY2( validation.valid && validation.managedDirectory, qPrintable( validation.error ) );
            StorageContext reopened( { mode, restartRoot, {}, true } );
            QFile restored( reopened.sessionFilePath() );
            QVERIFY( restored.open( QIODevice::ReadOnly ) );
            QCOMPARE( restored.readAll(), QByteArray( "existing session" ) );
        }
    }

    void productionFactoryOpensOnlyWithEveryCondition()
    {
        ApplicationUpdateSessionInputs inputs;
        inputs.registeredInstallRoot = QStringLiteral( "C:\\Program Files\\ZzLogg" );
        inputs.verifiedPackagePath
            = QStringLiteral( "C:\\Users\\u\\AppData\\Local\\ZzLogg\\cache\\setup.exe" );
        inputs.packageSize = 1024;
        inputs.packageSha256 = QString( 64, QLatin1Char( 'a' ) );
        inputs.releaseIdentityAvailable = true;

        auto missingRoot = inputs;
        missingRoot.registeredInstallRoot.clear();
        QVERIFY( makeUpdateSessionFactory( missingRoot ) == nullptr );

        auto missingPackage = inputs;
        missingPackage.verifiedPackagePath.clear();
        QVERIFY( makeUpdateSessionFactory( missingPackage ) == nullptr );

        auto missingSize = inputs;
        missingSize.packageSize = 0;
        QVERIFY( makeUpdateSessionFactory( missingSize ) == nullptr );

        auto shortDigest = inputs;
        shortDigest.packageSha256 = QStringLiteral( "abc" );
        QVERIFY( makeUpdateSessionFactory( shortDigest ) == nullptr );

        auto upperDigest = inputs;
        upperDigest.packageSha256 = QString( 64, QLatin1Char( 'A' ) );
        QVERIFY( makeUpdateSessionFactory( upperDigest ) == nullptr );

        auto unofficial = inputs;
        unofficial.releaseIdentityAvailable = false;
        QVERIFY( makeUpdateSessionFactory( unofficial ) == nullptr );

#ifdef Q_OS_WIN
        QVERIFY( makeUpdateSessionFactory( inputs ) != nullptr );
#else
        QVERIFY( makeUpdateSessionFactory( inputs ) == nullptr );
#endif
    }
};

QTEST_GUILESS_MAIN( UpdateSessionFactoryTest )
#include "updatesessionfactorytest.moc"
