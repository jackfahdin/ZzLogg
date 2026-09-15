#include <QtTest>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QPointer>
#include "zzlogg/updateqt/packagedownloader.h"
#include "../update/fixturehelper.h"
#include "scriptednetwork.h"
using namespace zzlogg::updateqt;
namespace {
const auto context=update_fixture::context();
zzlogg::update::Artifact signedArtifact(const QByteArray& bytes) {
    auto payload=update_fixture::payload();
    payload["artifacts"][0]["size"]=std::to_string(bytes.size());
    payload["artifacts"][0]["sha256"]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
    auto verified=zzlogg::update::verifyManifest(update_fixture::envelope(payload.dump()),context);
    if(!verified.value) qFatal("signed package fixture must verify");
    return verified.value->manifest().artifacts.at(0);
}
}
class PackageDownloaderTest : public QObject {
    Q_OBJECT
private slots:
    // Missing streaming/commit, trusting declared length, or skipping verification must fail these.
    void responses_data() {
        QTest::addColumn<int>("scenario"); QTest::addColumn<int>("expected");
        QTest::newRow("signed-success") << 0 << -1;
        QTest::newRow("unknown-length") << 1 << -1;
        QTest::newRow("finished-only") << 2 << -1;
        QTest::newRow("short-body") << 3 << int(DownloadError::SizeMismatch);
        QTest::newRow("long-body") << 4 << int(DownloadError::SizeMismatch);
        QTest::newRow("bad-hash") << 5 << int(DownloadError::HashMismatch);
        QTest::newRow("gzip") << 6 << int(DownloadError::Http);
        QTest::newRow("partial") << 7 << int(DownloadError::Http);
        QTest::newRow("not-modified") << 8 << int(DownloadError::Http);
        QTest::newRow("not-found") << 9 << int(DownloadError::Http);
        QTest::newRow("bad-length") << 10 << int(DownloadError::Http);
        QTest::newRow("wrong-length") << 11 << int(DownloadError::SizeMismatch);
        QTest::newRow("tls") << 12 << int(DownloadError::Tls);
        QTest::newRow("tls-network-error") << 13 << int(DownloadError::Tls);
        QTest::newRow("network") << 14 << int(DownloadError::Network);
        QTest::newRow("unexpected-final-url") << 15 << int(DownloadError::InvalidUrl);
        QTest::newRow("overflow-length") << 16 << int(DownloadError::Http);
        QTest::newRow("list-length") << 17 << int(DownloadError::Http);
    }
    void responses() {
        QFETCH(int,scenario); QFETCH(int,expected);
        const QByteArray expectedBytes(140000,'p'); const auto artifact=signedArtifact(expectedBytes);
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        NetworkScript script; script.body=expectedBytes; script.length="140000"; script.chunkSize=70000;
        switch(scenario) {
        case 1: script.length.clear(); break;
        case 2: script.finishedOnly=true; break;
        case 3: script.body.chop(1); break;
        case 4: script.body.append('x'); break;
        case 5: script.body[0]='x'; break;
        case 6: script.encoding="gzip"; break;
        case 7: script.status=206; break;
        case 8: script.status=304; break;
        case 9: script.status=404; script.error=QNetworkReply::ContentNotFoundError; break;
        case 10: script.length="-1"; break;
        case 11: script.length="140001"; break;
        case 12: script.tlsError=true; break;
        case 13: script.error=QNetworkReply::SslHandshakeFailedError; break;
        case 14: script.error=QNetworkReply::RemoteHostClosedError; break;
        case 15: script.finalUrl=QUrl("https://evil.invalid/package"); break;
        case 16: script.length="18446744073709551616"; break;
        case 17: script.length="140000,140000"; break;
        }
        network->scripts.push_back(script);
        PackageDownloader downloader(network,2000,500);
        QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
        QSignalSpy progress(&downloader,&PackageDownloader::progress);
        downloader.start(artifact,context.allowedHosts,root.path());
        QTRY_COMPARE_WITH_TIMEOUT(success.count()+failure.count(),1,2000);
        if(expected<0) {
            QCOMPARE(success.count(),1); QCOMPARE(failure.count(),0);
            QFile package(success[0][0].toString()); QVERIFY(package.open(QIODevice::ReadOnly));
            QCOMPARE(package.readAll(),expectedBytes);
            QVERIFY(!progress.isEmpty()); QCOMPARE(progress.last()[0].toLongLong(),qint64(expectedBytes.size()));
        } else {
            QCOMPARE(success.count(),0); QCOMPARE(failure.count(),1);
            QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError(expected));
            QVERIFY(QDir(root.path()).entryList({"*.package"},QDir::Files).isEmpty());
        }
        qint64 previous=0;
        for(const auto& row:progress) { QVERIFY(row[0].toLongLong()>previous); previous=row[0].toLongLong(); QCOMPARE(row[1].toLongLong(),qint64(expectedBytes.size())); }
        QVERIFY(script.metrics->maximumRequest<=64*1024);
        QCOMPARE(script.metrics->bufferLimit,qint64(128*1024));
        QCOMPARE(network->requests.size(),1);
        const auto request=network->requests[0];
        QCOMPARE(request.rawHeader("Accept-Encoding"),QByteArray("identity"));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(),int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt(),int(QNetworkRequest::Manual));
        QCOMPARE(request.attribute(QNetworkRequest::CacheLoadControlAttribute).toInt(),int(QNetworkRequest::AlwaysNetwork));
        QCOMPARE(request.attribute(QNetworkRequest::CacheSaveControlAttribute).toBool(),false);
    }
    void redirects_data() {
        QTest::addColumn<QByteArray>("location"); QTest::addColumn<bool>("valid");
        QTest::newRow("relative") << QByteArray("../next?x=1") << true;
        QTest::newRow("same-host") << QByteArray("https://updates.example.invalid/next") << true;
        QTest::newRow("downgrade") << QByteArray("http://updates.example.invalid/next") << false;
        QTest::newRow("cross-host") << QByteArray("https://evil.invalid/next") << false;
        QTest::newRow("suffix-host") << QByteArray("https://updates.example.invalid.evil.invalid/x") << false;
        QTest::newRow("userinfo") << QByteArray("https://user@updates.example.invalid/x") << false;
        QTest::newRow("fragment") << QByteArray("/a#b") << false;
        QTest::newRow("space") << QByteArray("/a b") << false;
        QTest::newRow("bad-escape") << QByteArray("/a%XX") << false;
        QTest::newRow("control") << QByteArray("/a%0a") << false;
        QTest::newRow("backslash") << QByteArray("/a\\b") << false;
        QTest::newRow("unicode") << QByteArray("/a\xc3\xa9") << false;
        QTest::newRow("oversize") << QByteArray(8193,'a') << false;
        QTest::newRow("empty") << QByteArray() << false;
    }
    void redirects() {
        QFETCH(QByteArray,location); QFETCH(bool,valid);
        QTemporaryDir root; const auto artifact=signedArtifact("package");
        auto* network=new ScriptedNetworkManager;
        NetworkScript redirect; redirect.status=302; redirect.location=location; redirect.body=QByteArray(256*1024,'r'); redirect.chunkSize=256*1024;
        NetworkScript final; final.body="package";
        network->scripts={redirect,final}; PackageDownloader downloader(network,2000,500);
        QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
        downloader.start(artifact,context.allowedHosts,root.path());
        QTRY_COMPARE(success.count()+failure.count(),1);
        QCOMPARE(success.count(),valid?1:0); QCOMPARE(network->requests.size(),valid?2:1);
        if(!valid) QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError::Redirect);
        QVERIFY(redirect.metrics->totalRead<=64*1024);
    }
    void redirectBudget() {
        for(int count:{3,4}) {
            QTemporaryDir root; auto* network=new ScriptedNetworkManager;
            NetworkScript redirect; redirect.status=307; redirect.location="/next";
            for(int i=0;i<count;++i) network->scripts.push_back(redirect);
            NetworkScript final; final.body="package"; network->scripts.push_back(final);
            PackageDownloader downloader(network,2000,500);
            QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
            downloader.start(signedArtifact("package"),context.allowedHosts,root.path());
            QTRY_COMPARE(success.count()+failure.count(),1);
            QCOMPARE(success.count(),count==3?1:0); QCOMPARE(network->requests.size(),4);
            if(count==4) QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError::Redirect);
        }
    }
    void deadlinesAndCancellation() {
        for(int scenario:{0,1,2}) {
            QTemporaryDir root; auto* network=new ScriptedNetworkManager;
            NetworkScript script; script.body=QByteArray(100,'p'); script.hang=scenario!=1; script.chunkSize=1; script.intervalMs=10;
            network->scripts.push_back(script); PackageDownloader downloader(network,100,50);
            QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
            downloader.start(signedArtifact(script.body),context.allowedHosts,root.path());
            if(scenario==2) { downloader.cancel(); downloader.cancel(); }
            QTRY_COMPARE(failure.count(),1);
            QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),scenario==2?DownloadError::Cancelled:DownloadError::Timeout);
            QTest::qWait(100); QCOMPARE(failure.count(),1); QCOMPARE(success.count(),0);
            QVERIFY(QDir(root.path()).entryList({"*.package"},QDir::Files).isEmpty());
        }
    }
    void replacementIgnoresLateFinish() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        NetworkScript hanging; hanging.hang=true; NetworkScript next; next.body="package";
        network->scripts={hanging,next}; PackageDownloader downloader(network,2000,500);
        QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
        const auto artifact=signedArtifact(next.body);
        downloader.start(artifact,context.allowedHosts,root.path()); downloader.start(artifact,context.allowedHosts,root.path());
        QTRY_COMPARE(success.count(),1); QTest::qWait(50); QCOMPARE(success.count(),1); QCOMPARE(failure.count(),0);
    }
    void signalReentry_data() {
        QTest::addColumn<int>("signal"); QTest::addColumn<bool>("destroy");
        for(int signal:{0,1,2}) for(bool destroy:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(signal).arg(destroy))) << signal << destroy;
    }
    void signalReentry() {
        QFETCH(int,signal); QFETCH(bool,destroy);
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        NetworkScript first; first.body=QByteArray(150000,'p'); first.chunkSize=150000;
        if(signal==1) { first.encoding="gzip"; first.finishedOnly=true; }
        NetworkScript next; next.body=first.body; network->scripts={first,next};
        QPointer<PackageDownloader> downloader=new PackageDownloader(network,2000,500);
        const auto artifact=signedArtifact(next.body); int successes=0, failures=0; bool handled=false;
        connect(downloader,&PackageDownloader::succeeded,this,[&]{++successes;});
        connect(downloader,&PackageDownloader::failed,this,[&]{++failures;});
        auto action=[&] {
            if(handled) return; handled=true;
            if(destroy) delete downloader.data();
            else downloader->start(artifact,context.allowedHosts,root.path());
        };
        if(signal==0) connect(downloader,&PackageDownloader::progress,this,action);
        if(signal==1) connect(downloader,&PackageDownloader::failed,this,action);
        if(signal==2) connect(downloader,&PackageDownloader::succeeded,this,action);
        downloader->start(artifact,context.allowedHosts,root.path());
        QTRY_VERIFY(handled);
        if(destroy) QVERIFY(downloader.isNull());
        else { QTRY_COMPARE(successes,signal==2?2:1); delete downloader.data(); }
        QTest::qWait(30); QCOMPARE(failures,signal==1?1:0);
    }
    void largeFinishedBodyYieldsToEventLoop() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        NetworkScript script; script.body=QByteArray(3*1024*1024,'p'); script.chunkSize=script.body.size(); script.finishedOnly=true;
        network->scripts.push_back(script); PackageDownloader downloader(network,2000,500);
        qint64 received=0, observed=-1;
        connect(&downloader,&PackageDownloader::progress,this,[&](qint64 value,qint64){
            received=value;
            if(observed==-1) { observed=-2; QTimer::singleShot(0,&downloader,[&]{observed=received;}); }
        });
        QSignalSpy success(&downloader,&PackageDownloader::succeeded);
        downloader.start(signedArtifact(script.body),context.allowedHosts,root.path());
        QTRY_COMPARE(success.count(),1); QVERIFY(observed>0); QVERIFY(observed<=1024*1024);
        QVERIFY(script.metrics->maximumRequest<=64*1024);
    }
    void invalidStartNeverRequests() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager; PackageDownloader downloader(network,2000,500);
        QSignalSpy failure(&downloader,&PackageDownloader::failed);
        auto artifact=signedArtifact("package"); artifact.url="http://updates.example.invalid/x";
        downloader.start(artifact,context.allowedHosts,root.path());
        QCOMPARE(failure.count(),1); QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError::InvalidUrl);
        QVERIFY(network->requests.isEmpty());
    }
    void cacheErrorsRemainDistinct() {
        QTemporaryDir root; const auto artifact=signedArtifact("package");
        PackageCache lock(root.path(),artifact); QCOMPARE(lock.begin(),CacheError::None);
        auto* network=new ScriptedNetworkManager; PackageDownloader downloader(network,2000,500);
        QSignalSpy failure(&downloader,&PackageDownloader::failed);
        downloader.start(artifact,context.allowedHosts,root.path());
        QCOMPARE(failure.count(),1); QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError::Busy);
        downloader.start(artifact,context.allowedHosts,QStringLiteral("relative-cache"));
        QCOMPARE(failure.count(),2); QCOMPARE(qvariant_cast<DownloadError>(failure[1][0]),DownloadError::InvalidPath);
        auto malformed=artifact; malformed.sha256="bad";
        downloader.start(malformed,context.allowedHosts,root.path());
        QCOMPARE(failure.count(),3); QCOMPARE(qvariant_cast<DownloadError>(failure[2][0]),DownloadError::InvalidArtifact);
        QVERIFY(network->requests.isEmpty());
    }
    void queuedConsumptionCanCancelAndRestart() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        NetworkScript first; first.body=QByteArray(3*1024*1024,'p'); first.chunkSize=int(first.body.size()); first.finishedOnly=true;
        NetworkScript next; next.body="replacement"; network->scripts={first,next};
        PackageDownloader downloader(network,2000,500);
        QSignalSpy success(&downloader,&PackageDownloader::succeeded), failure(&downloader,&PackageDownloader::failed);
        bool scheduled=false;
        connect(&downloader,&PackageDownloader::progress,this,[&] {
            if(scheduled) return; scheduled=true;
            QTimer::singleShot(0,&downloader,[&] {
                downloader.cancel();
                downloader.start(signedArtifact(next.body),context.allowedHosts,root.path());
            });
        });
        downloader.start(signedArtifact(first.body),context.allowedHosts,root.path());
        QTRY_COMPARE(success.count(),1); QCOMPARE(failure.count(),1);
        QCOMPARE(qvariant_cast<DownloadError>(failure[0][0]),DownloadError::Cancelled);
        QFile package(success[0][0].toString()); QVERIFY(package.open(QIODevice::ReadOnly)); QCOMPARE(package.readAll(),next.body);
        QVERIFY(first.metrics->totalRead<=1024*1024);
        QTest::qWait(50); QCOMPARE(success.count(),1); QCOMPARE(failure.count(),1);
    }
    void destructionCancelsSilentlyAndReleasesCache() {
        QTemporaryDir root; const auto artifact=signedArtifact("package");
        auto* network=new ScriptedNetworkManager; NetworkScript hanging; hanging.hang=true; network->scripts.push_back(hanging);
        auto* downloader=new PackageDownloader(network,100,50); int callbacks=0;
        connect(downloader,&PackageDownloader::failed,this,[&]{++callbacks;});
        connect(downloader,&PackageDownloader::succeeded,this,[&]{++callbacks;});
        downloader->start(artifact,context.allowedHosts,root.path()); delete downloader;
        PackageCache replacement(root.path(),artifact); QCOMPARE(replacement.begin(),CacheError::None);
        QTest::qWait(150); QCOMPARE(callbacks,0);
        QVERIFY(QDir(root.path()).entryList({"*.package"},QDir::Files).isEmpty());
    }
};
QTEST_GUILESS_MAIN(PackageDownloaderTest)
#include "packagedownloadertest.moc"
