#include "zzlogg_brand.h"
#include "issuereporter.h"
#include "zzlogg/updateqt/updateservice.h"
#include <QTemporaryDir>
#include <QFile>

#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

class BrandContractTest final : public QObject {
    Q_OBJECT

  private slots:
    void exposesApprovedIdentity();
    void disabledUpdateCheckDoesNotWriteState();
};

void BrandContractTest::exposesApprovedIdentity()
{
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductName ), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductDescription ),
              QStringLiteral( "ZzLogg log viewer" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::Vendor ), QStringLiteral( "Jackfahdin" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::HomepageUrl ),
              QStringLiteral( "https://github.com/jackfahdin/ZzLogg" ) );
    QCOMPARE( QString::fromLatin1( zzlogg::brand::ApplicationIdentifier ),
              QStringLiteral( "com.gitcode.jackfahdinqt.zzlogg" ) );

    const QUrl issueUrl = IssueReporter::issueUrl( IssueTemplate::Bug );
    QCOMPARE( issueUrl.scheme(), QStringLiteral( "https" ) );
    QCOMPARE( issueUrl.host(), QStringLiteral( "github.com" ) );
    QCOMPARE( issueUrl.path(), QStringLiteral( "/jackfahdin/ZzLogg/issues/new" ) );
    const QString issueBody
        = QUrlQuery( issueUrl ).queryItemValue( QStringLiteral( "body" ) );
    QVERIFY( issueBody.contains( QStringLiteral( "> ZzLogg version " ) ) );
    QVERIFY( issueBody.contains(
        QStringLiteral( "> running on %1" ).arg( QSysInfo::prettyProductName() ) ) );
}

void BrandContractTest::disabledUpdateCheckDoesNotWriteState()
{
    using namespace zzlogg::updateqt;
    QTemporaryDir directory;
    const auto path=directory.filePath("state.json");
    UpdateService service({},std::make_shared<UpdateStateStore>(path),{},[]{return 1800000000;});
    service.requestCheck(Channel::Stable,CheckOrigin::Manual);
    QCOMPARE(service.snapshot().status,CheckStatus::NotConfigured);
    QVERIFY(!QFile::exists(path));
}

QTEST_GUILESS_MAIN( BrandContractTest )

#include "brandcontracttest.moc"
