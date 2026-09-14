#include "zzlogg_brand.h"
#include "issuereporter.h"
#include "versionchecker.h"

#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

class BrandContractTest final : public QObject {
    Q_OBJECT

  private slots:
    void exposesApprovedIdentity();
    void disabledUpdateCheckCreatesNoNetworkManager();
};

void BrandContractTest::exposesApprovedIdentity()
{
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductName ), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductDescription ),
              QStringLiteral( "ZzLogg log viewer" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::Vendor ), QStringLiteral( "Jackfahdin" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::HomepageUrl ),
              QStringLiteral( "https://gitcode.com/JackfahdinQt/ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ApplicationIdentifier ),
              QStringLiteral( "com.gitcode.jackfahdinqt.zzlogg" ) );
    QVERIFY( QString::fromLatin1( zzlogg::brand::UpdateManifestUrl ).isEmpty() );

    QVERIFY( !VersionChecker::isUpdateCheckConfigured() );

    const QUrl issueUrl = IssueReporter::issueUrl( IssueTemplate::Bug );
    QCOMPARE( issueUrl.scheme(), QStringLiteral( "https" ) );
    QCOMPARE( issueUrl.host(), QStringLiteral( "gitcode.com" ) );
    QCOMPARE( issueUrl.path(), QStringLiteral( "/JackfahdinQt/ZzLogg/issues/new" ) );
    const QString issueBody
        = QUrlQuery( issueUrl ).queryItemValue( QStringLiteral( "body" ) );
    QVERIFY( issueBody.contains( QStringLiteral( "> ZzLogg version " ) ) );
    QVERIFY( issueBody.contains(
        QStringLiteral( "> running on %1" ).arg( QSysInfo::prettyProductName() ) ) );
}

void BrandContractTest::disabledUpdateCheckCreatesNoNetworkManager()
{
    VersionChecker checker;
    checker.startCheck();

    QVERIFY( checker.findChildren<QNetworkAccessManager*>(
                         QString(), Qt::FindDirectChildrenOnly )
                 .isEmpty() );
}

QTEST_GUILESS_MAIN( BrandContractTest )

#include "brandcontracttest.moc"
