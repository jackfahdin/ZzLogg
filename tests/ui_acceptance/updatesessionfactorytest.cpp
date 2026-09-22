#include "applicationupdatesession.h"

#include <QtTest>

class UpdateSessionFactoryTest : public QObject {
    Q_OBJECT
private slots:
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
