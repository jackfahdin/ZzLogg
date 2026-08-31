#include "zzlogg_brand.h"

#include <QtTest>

class BrandContractTest final : public QObject {
    Q_OBJECT

  private slots:
    void exposesApprovedIdentity();
};

void BrandContractTest::exposesApprovedIdentity()
{
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductName ), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductDescription ),
              QStringLiteral( "ZzLogg log viewer" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::Vendor ), QStringLiteral( "JackfahdinQt" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::HomepageUrl ),
              QStringLiteral( "https://gitcode.com/JackfahdinQt/ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ApplicationIdentifier ),
              QStringLiteral( "com.gitcode.jackfahdinqt.zzlogg" ) );
    QVERIFY( QString::fromLatin1( zzlogg::brand::UpdateManifestUrl ).isEmpty() );
}

QTEST_APPLESS_MAIN( BrandContractTest )

#include "brandcontracttest.moc"
