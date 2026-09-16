#include <QtTest>
#include <QTcpServer>
#include "zzlogg/updateqt/manifestfetcher.h"
#include "scriptednetwork.h"
using namespace zzlogg::updateqt;
namespace {
const QUrl feed("https://updates.example.invalid/manifest");
const std::vector<std::string> hosts{"updates.example.invalid"};
}
class FetcherTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsDowngrade() {
        auto* network=new ScriptedNetworkManager;
        NetworkScript redirect; redirect.status=302; redirect.location="http://updates.example.invalid/manifest";
        network->scripts.push_back(redirect);
        ManifestFetcher fetcher(network,1000,500);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        fetcher.start(feed,hosts);
        QTRY_COMPARE(failure.count(),1);
        QCOMPARE(success.count(),0);
        QCOMPARE(network->requests.size(),1);
    }
    void responses_data() {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("expected");
        QTest::newRow("success") << 0 << -1;
        QTest::newRow("exact-limit") << 1 << -1;
        QTest::newRow("chunked-too-large") << 2 << int(FetchError::TooLarge);
        QTest::newRow("declared-too-large") << 3 << int(FetchError::TooLarge);
        QTest::newRow("understated-length") << 4 << int(FetchError::Http);
        QTest::newRow("overstated-length") << 5 << int(FetchError::Http);
        QTest::newRow("empty") << 6 << int(FetchError::Http);
        QTest::newRow("gzip") << 7 << int(FetchError::Http);
        QTest::newRow("not-modified") << 8 << int(FetchError::Http);
        QTest::newRow("not-found") << 9 << int(FetchError::Http);
        QTest::newRow("tls") << 10 << int(FetchError::Tls);
        QTest::newRow("unexpected-final-url") << 11 << int(FetchError::InvalidUrl);
        QTest::newRow("transport-error") << 12 << int(FetchError::Network);
        QTest::newRow("bad-length") << 13 << int(FetchError::Http);
    }
    void responses() {
        QFETCH(int,scenario); QFETCH(int,expected);
        auto* network=new ScriptedNetworkManager;
        NetworkScript script;
        switch(scenario) {
        case 1: script.body=QByteArray(256*1024,'a'); break;
        case 2: script.body=QByteArray(256*1024+1,'a'); break;
        case 3: script.length="262145"; break;
        case 4: script.length="1"; break;
        case 5: script.length="3"; break;
        case 6: script.body.clear(); break;
        case 7: script.encoding="gzip"; break;
        case 8: script.status=304; break;
        case 9: script.status=404; break;
        case 10: script.tlsError=true; break;
        case 11: script.finalUrl=QUrl("https://updates.example.invalid.evil.test/feed"); break;
        case 12: script.error=QNetworkReply::RemoteHostClosedError; break;
        case 13: script.length="-1"; break;
        }
        network->scripts.push_back(script);
        ManifestFetcher fetcher(network,2000,500);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        fetcher.start(feed,hosts);
        QTRY_COMPARE(success.count()+failure.count(),1);
        if(expected<0) { QCOMPARE(success.count(),1); QCOMPARE(success[0][0].toByteArray(),script.body); }
        else { QCOMPARE(failure.count(),1); QCOMPARE(qvariant_cast<FetchError>(failure[0][0]),FetchError(expected)); }
        QCOMPARE(network->requests.size(),1);
        const auto request=network->requests[0];
        QVERIFY(script.metrics->totalRead<=256*1024+1);
        QVERIFY(script.metrics->maximumRequest<=16*1024);
        QCOMPARE(script.metrics->bufferLimit,qint64(16*1024));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.rawHeader("Accept-Encoding"),QByteArray("identity"));
        QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),int(QNetworkRequest::Manual));
    }
    void redirects_data() {
        QTest::addColumn<QByteArray>("location");
        QTest::addColumn<bool>("valid");
        QTest::newRow("relative") << QByteArray("../next?x=1") << true;
        QTest::newRow("same-host") << QByteArray("https://updates.example.invalid/next") << true;
        QTest::newRow("suffix-host") << QByteArray("https://updates.example.invalid.evil.test/a") << false;
        QTest::newRow("userinfo") << QByteArray("https://user@updates.example.invalid/a") << false;
        QTest::newRow("fragment") << QByteArray("/a#b") << false;
        QTest::newRow("space-inside-url") << QByteArray("/a b") << false;
        QTest::newRow("bad-escape") << QByteArray("/a%XX") << false;
        QTest::newRow("control-escape") << QByteArray("/a%0a") << false;
        QTest::newRow("backslash") << QByteArray("/a\\b") << false;
        QTest::newRow("unicode") << QByteArray("/a\xC3\xA9") << false;
        QTest::newRow("oversize") << QByteArray(8193,'a') << false;
    }
    void redirects() {
        QFETCH(QByteArray,location); QFETCH(bool,valid);
        auto* network=new ScriptedNetworkManager;
        NetworkScript redirect; redirect.status=302; redirect.location=location;
        network->scripts={redirect,NetworkScript{}};
        ManifestFetcher fetcher(network,1000,500);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        fetcher.start(feed,hosts);
        QTRY_COMPARE(success.count()+failure.count(),1);
        QCOMPARE(success.count(),valid ? 1 : 0);
        QCOMPARE(network->requests.size(),valid ? 2 : 1);
    }
    void redirectBudget() {
        for(int count:{3,4}) {
            auto* network=new ScriptedNetworkManager;
            for(int i=0;i<count;++i) { NetworkScript s; s.status=307; s.location="/next"; network->scripts.push_back(s); }
            network->scripts.push_back(NetworkScript{});
            ManifestFetcher fetcher(network,1000,500);
            QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
            fetcher.start(feed,hosts);
            QTRY_COMPARE(success.count()+failure.count(),1);
            QCOMPARE(success.count(),count==3 ? 1 : 0);
            QCOMPARE(network->requests.size(),4);
        }
    }
    void timeoutsAndCancel() {
        for(int scenario:{0,1,2}) {
            auto* network=new ScriptedNetworkManager;
            NetworkScript script;
            script.hang=scenario!=1;
            script.body=QByteArray(100,'a'); script.chunkSize=1; script.intervalMs=10;
            network->scripts.push_back(script);
            ManifestFetcher fetcher(network,100,50);
            QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
            fetcher.start(feed,hosts);
            if(scenario==2) { fetcher.cancel(); fetcher.cancel(); }
            QTRY_COMPARE(failure.count(),1);
            QCOMPARE(qvariant_cast<FetchError>(failure[0][0]),scenario==2 ? FetchError::Cancelled : FetchError::Timeout);
            QTest::qWait(100);
            QCOMPARE(failure.count(),1); QCOMPARE(success.count(),0);
        }
    }
    void replacementAndDestruction() {
        auto* network=new ScriptedNetworkManager;
        NetworkScript hanging; hanging.hang=true;
        network->scripts={hanging,NetworkScript{}};
        ManifestFetcher fetcher(network,1000,500);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        fetcher.start(feed,hosts); fetcher.start(feed,hosts);
        QTRY_COMPARE(success.count(),1); QCOMPARE(failure.count(),0);
        int callbacks=0;
        auto* otherNetwork=new ScriptedNetworkManager;
        otherNetwork->scripts.push_back(hanging);
        auto* other=new ManifestFetcher(otherNetwork,100,50);
        connect(other,&ManifestFetcher::failed,this,[&]{++callbacks;});
        connect(other,&ManifestFetcher::succeeded,this,[&]{++callbacks;});
        other->start(feed,hosts); delete other;
        QTest::qWait(150); QCOMPARE(callbacks,0);
    }
    void redirectDoesNotResetTotalDeadline() {
        auto* network=new ScriptedNetworkManager;
        NetworkScript redirect; redirect.status=308; redirect.location="/next";
        redirect.responseDelayMs=200;
        NetworkScript finalResponse; finalResponse.responseDelayMs=400;
        network->scripts={redirect,finalResponse};
        // The final response would arrive at 600 ms: after the original 500 ms
        // deadline, but before a wrongly restarted deadline at 700 ms.
        ManifestFetcher fetcher(network,500,1000);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        fetcher.start(feed,hosts);
        QTRY_COMPARE(failure.count(),1);
        QCOMPARE(qvariant_cast<FetchError>(failure[0][0]),FetchError::Timeout);
        QCOMPARE(success.count(),0);
        QCOMPARE(network->requests.size(),2);
    }
    void realHttpNeverConnects() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        ManifestFetcher fetcher;
        QSignalSpy failure(&fetcher,&ManifestFetcher::failed), connections(&server,&QTcpServer::newConnection);
        fetcher.start(QUrl(QString("http://127.0.0.1:%1/feed").arg(server.serverPort())),{"127.0.0.1"});
        QCOMPARE(failure.count(),1);
        QCOMPARE(qvariant_cast<FetchError>(failure[0][0]),FetchError::InvalidUrl);
        QTest::qWait(100); QCOMPARE(connections.count(),0);
    }
    void qtNormalizesHeaderPadding() {
        NetworkScript script; script.location=" /a ";
        ScriptedReply reply(QNetworkRequest(feed),script,this);
        QCOMPARE(reply.rawHeader("Location"),QByteArray("/a"));
    }
    void errorCallbackCanStartNewRequest() {
        auto* network=new ScriptedNetworkManager;
        NetworkScript invalid; invalid.encoding="gzip"; invalid.finishedOnly=true;
        NetworkScript next;
        network->scripts={invalid,next};
        ManifestFetcher fetcher(network,1000,500);
        QSignalSpy success(&fetcher,&ManifestFetcher::succeeded), failure(&fetcher,&ManifestFetcher::failed);
        connect(&fetcher,&ManifestFetcher::failed,this,[&]{
            if(network->requests.size()==1) fetcher.start(feed,hosts);
        });
        fetcher.start(feed,hosts);
        QTRY_COMPARE_WITH_TIMEOUT(success.count(),1,1000);
        QCOMPARE(failure.count(),1);
    }
    void errorCallbackCanDestroyFetcher() {
        auto* network=new ScriptedNetworkManager;
        NetworkScript invalid; invalid.encoding="gzip"; invalid.finishedOnly=true;
        network->scripts.push_back(invalid);
        QPointer<ManifestFetcher> fetcher=new ManifestFetcher(network,1000,500);
        int callbacks=0;
        connect(fetcher,&ManifestFetcher::failed,this,[&]{ ++callbacks; delete fetcher.data(); });
        fetcher->start(feed,hosts);
        QTRY_VERIFY(fetcher.isNull());
        QCOMPARE(callbacks,1);
    }
};
QTEST_GUILESS_MAIN(FetcherTest)
#include "manifestfetchertest.moc"
