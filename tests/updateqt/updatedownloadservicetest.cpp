#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include "zzlogg/updateqt/updatedownloadservice.h"
#include "zzlogg/updateqt/updatecachepaths.h"
#include "../update/fixturehelper.h"
#include "scriptednetwork.h"
using namespace zzlogg::updateqt;
using namespace zzlogg::update;
namespace {
FeedConfiguration configuration() {
    const auto c=update_fixture::context();
    return {"https://updates.example.invalid/stable","https://updates.example.invalid/preview",
        c.keys,c.allowedHosts,c.buildTime,c.environment};
}
InstalledRelease installedRelease() {
    InstalledRelease result; result.version={26,9,0}; result.releaseSequence=1;
    result.distribution=Distribution::Installer;
    result.osVersion={10,0,22631}; result.dataSchema=1; return result;
}
CheckSnapshot available(const QByteArray& bytes="package",int sequence=2,Channel channel=Channel::Stable) {
    auto payload=update_fixture::payload();
    payload["metadataSequence"]=std::to_string(sequence);
    payload["releaseSequence"]=std::to_string(sequence);
    payload["channel"]=channel==Channel::Stable?"stable":"preview";
    payload["artifacts"][0]["distribution"]="installer";
    payload["artifacts"][0]["format"]="nsis-exe";
    payload["artifacts"][0]["minOsVersion"]="10.0.22631";
    payload["artifacts"][0]["url"]="https://updates.example.invalid/ZzLoggSetup.exe";
    payload["artifacts"][0]["size"]=std::to_string(bytes.size());
    payload["artifacts"][0]["sha256"]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
    auto context=update_fixture::context(); context.channel=payload["channel"];
    const auto envelope="\n"+update_fixture::envelope(payload.dump())+"\r\n";
    auto verified=verifyManifest(envelope,context);
    if(!verified.value) qFatal("signed download fixture must verify");
    return {CheckStatus::Available,channel,verified.value,Decision{DecisionStatus::Available,{}},true,context.now};
}
NetworkScript response(const QByteArray& bytes="package") { NetworkScript s; s.body=bytes; return s; }
}
class DownloadServiceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    // Trusting public status/decision, missing configuration or time/identity guards must fail.
    void rejectedInputs_data() {
        QTest::addColumn<int>("scenario");
        int index=0;
        for(const auto& row:std::initializer_list<const char*>{"unconfigured","no-installed","development",
            "zero-sequence","no-manifest","information","not-configured","expired","not-issued",
            "channel-mismatch","no-update","incompatible","wrong-key","test-to-production",
            "test-without-test-mode","wrong-key-id","wrong-key-purpose","invalid-channel","host-removed"})
            QTest::newRow(row) << index++;
    }
    void rejectedInputs() {
        QFETCH(int,scenario);
        QTemporaryDir root; const auto path=root.filePath("not-created/cache");
        auto c=configuration(); std::optional<InstalledRelease> installed=installedRelease();
        auto check=available(); qint64 now=1800000000;
        if(scenario==0) c=productionFeedConfiguration();
        if(scenario==1) installed.reset();
        if(scenario==2) installed->developmentBuild=true;
        if(scenario==3) installed->releaseSequence=0;
        if(scenario==4) check.release.reset();
        if(scenario==5) check.status=CheckStatus::ReleaseInformation;
        if(scenario==6) check.status=CheckStatus::NotConfigured;
        if(scenario==7) now=1800003600;
        if(scenario==8) now=1799999899;
        if(scenario==9) check.channel=Channel::Preview;
        if(scenario==10) { installed->version={26,10,0}; installed->releaseSequence=2; }
        if(scenario==11) installed->arch="arm64";
        if(scenario==12) c.keys[0].publicKey[0]^=1;
        if(scenario==13) { c.environment=TrustEnvironment::Production; c.keys[0].purpose=KeyPurpose::Production; }
        if(scenario==14) QStandardPaths::setTestModeEnabled(false);
        if(scenario==15) c.keys[0].id="rotated";
        if(scenario==16) c.keys[0].purpose=KeyPurpose::Production;
        if(scenario==17) check.channel=static_cast<Channel>(99);
        if(scenario==18) c.allowedHosts={"other.invalid"};
        auto* network=new ScriptedNetworkManager;
        UpdateDownloadService service(c,installed,path,[&]{return now;},nullptr,[&]{return network;});
        service.requestDownload(check);
        const auto result=service.snapshot();
        QStandardPaths::setTestModeEnabled(true);
        QCOMPARE(result.status,DownloadStatus::Unavailable);
        QVERIFY(result.verifiedPath.isEmpty()); QVERIFY(!result.selection); QVERIFY(network->requests.isEmpty());
        QVERIFY(!QFileInfo::exists(path));
    }
    // Re-selecting the signed artifact must defeat an attacker-controlled public decision.
    void signedChainIgnoresForgedDecision() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager; network->scripts={response()};
        auto check=available(); auto fake=check.release->manifest().artifacts[0];
        fake.url="https://evil.invalid/payload"; fake.sha256=std::string(64,'0'); fake.size=42;
        check.decision=Decision{DecisionStatus::NoUpdate,fake};
        UpdateDownloadService service(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestDownload(check);
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Verified,1000);
        QCOMPARE(network->requests.size(),1);
        QCOMPARE(network->requests[0].url(),QUrl("https://updates.example.invalid/ZzLoggSetup.exe"));
        QFile file(service.snapshot().verifiedPath); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),QByteArray("package")); QCOMPARE(service.snapshot().received,qint64(7));
        QCOMPARE(service.snapshot().total,qint64(7)); QVERIFY(!service.snapshot().error);
        QVERIFY(service.snapshot().selection);
        QCOMPARE(service.snapshot().selection->signedEnvelope,check.release->signedEnvelope());
        QCOMPARE(service.snapshot().selection->artifact.url,
                 std::string("https://updates.example.invalid/ZzLoggSetup.exe"));
    }
    // With the same signing key/purpose and all production preconditions satisfied,
    // removing only the environment check must admit the Test-verified snapshot.
    void productionServiceRejectsTestVerificationEnvironment() {
        // This deterministic key exists only inside this test; it is never deployed or configured globally.
        std::array<std::uint8_t,32> seed{}, publicKey{};
        for(std::size_t i=0;i<seed.size();++i) seed[i]=std::uint8_t(i);
        std::array<std::uint8_t,64> secret{}, signature{};
        crypto_ed25519_key_pair(secret.data(),publicKey.data(),seed.data());
        auto productionPayload=update_fixture::payload();
        productionPayload["artifacts"][0]["distribution"]="installer";
        productionPayload["artifacts"][0]["format"]="nsis-exe";
        const std::string id="environment-isolation-test", payload=productionPayload.dump();
        const auto message="ZzLogg update manifest v1\n"+id+"\n"+payload;
        crypto_ed25519_sign(signature.data(),secret.data(),
            reinterpret_cast<const std::uint8_t*>(message.data()),message.size());
        const auto envelope=nlohmann::json{{"schema",1},{"keyId",id},
            {"payload",zzlogg::update::detail::encodeBase64(payload)},
            {"signature",zzlogg::update::detail::encodeBase64(
                {reinterpret_cast<const char*>(signature.data()),signature.size()})}}.dump();
        auto config=configuration(); config.environment=TrustEnvironment::Production;
        config.keys={{id,{publicKey.begin(),publicKey.end()},KeyPurpose::Production}};
        QStandardPaths::setTestModeEnabled(false);
        struct RestoreTestMode { ~RestoreTestMode() { QStandardPaths::setTestModeEnabled(true); } } restore;
        const auto cache=updateCachePath(TrustEnvironment::Production);
        QVERIFY(!cache.isEmpty());
        const bool existed=QFileInfo::exists(cache);
        for(const auto environment:{TrustEnvironment::Test,TrustEnvironment::Production}) {
            auto context=update_fixture::context(); context.environment=environment; context.keys=config.keys;
            const auto verified=verifyManifest(envelope,context); QVERIFY(verified.value);
            auto* network=new ScriptedNetworkManager;
            UpdateDownloadService service(config,installedRelease(),cache,[]{return 1800000000;},nullptr,[&]{return network;});
            bool admitted=false;
            connect(&service,&UpdateDownloadService::snapshotChanged,this,[&]{
                if(service.snapshot().status==DownloadStatus::Downloading) {
                    admitted=true;
                    // Safety boundary: even the Production control or a mutated guard cannot reach disk/network.
                    service.cancel();
                }
            });
            service.requestDownload({CheckStatus::Available,Channel::Stable,verified.value,{},true,context.now});
            QVERIFY(network->requests.isEmpty()); QCOMPARE(QFileInfo::exists(cache),existed);
            QVERIFY(service.snapshot().verifiedPath.isEmpty());
            QCOMPARE(admitted,environment==TrustEnvironment::Production);
            QCOMPARE(service.snapshot().status,environment==TrustEnvironment::Production
                ? DownloadStatus::Cancelled : DownloadStatus::Unavailable);
        }
    }
    void duplicateAndDifferentReleaseRequireConfirmation() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        auto first=response(); first.hang=true; network->scripts={first,response("next")};
        UpdateDownloadService service(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
        const auto original=available(), next=available("next",3);
        service.requestDownload(original); service.requestDownload(original);
        QCOMPARE(network->requests.size(),1);
        service.requestDownload(next); QCOMPARE(service.snapshot().status,DownloadStatus::Unavailable);
        QCOMPARE(network->requests.size(),1); QVERIFY(service.snapshot().verifiedPath.isEmpty());
        QTest::qWait(20); QVERIFY(QDir(root.path()).entryList({"*.package"},QDir::Files).isEmpty());
        service.requestDownload(next);
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Verified,1000);
        service.requestDownload(next); QCOMPARE(network->requests.size(),2);
        QFile file(service.snapshot().verifiedPath); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),QByteArray("next"));
        service.invalidate(); QCOMPARE(service.snapshot().status,DownloadStatus::Idle); QVERIFY(service.snapshot().verifiedPath.isEmpty());
        QVERIFY(!service.snapshot().selection);
    }

    void updateServiceEnvelopeSurvivesIntoDownloadSelectionByValue() {
        QTemporaryDir stateRoot;
        auto store=std::make_shared<UpdateStateStore>(stateRoot.filePath("state.json"));
        auto payload=update_fixture::payload();
        payload["artifacts"][0]["distribution"]="installer";
        payload["artifacts"][0]["format"]="nsis-exe";
        payload["artifacts"][0]["minOsVersion"]="10.0.22631";
        payload["artifacts"][0]["url"]="https://updates.example.invalid/ZzLoggSetup.exe";
        payload["artifacts"][0]["size"]="7";
        payload["artifacts"][0]["sha256"]=QCryptographicHash::hash(
            QByteArray("package"),QCryptographicHash::Sha256).toHex().toStdString();
        const QByteArray exact=QByteArray("\n  ")+QByteArray::fromStdString(
            update_fixture::envelope(payload.dump()))+QByteArray("\r\n\t");
        CheckSnapshot checked;
        {
            auto* network=new ScriptedNetworkManager;
            NetworkScript script; script.body=exact; network->scripts={script};
            UpdateService service(configuration(),store,installedRelease(),[]{return 1800000000;},
                nullptr,[&]{return network;});
            service.requestCheck(Channel::Stable,CheckOrigin::Manual);
            QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,CheckStatus::Available,1000);
            checked=service.snapshot();
        }
        QVERIFY(checked.release);
        QCOMPARE(checked.release->signedEnvelope(),exact.toStdString());

        QTemporaryDir cacheRoot;
        auto* network=new ScriptedNetworkManager; network->scripts={response()};
        UpdateDownloadService download(configuration(),installedRelease(),cacheRoot.path(),
            []{return 1800000000;},nullptr,[&]{return network;});
        download.requestDownload(checked);
        QTRY_COMPARE_WITH_TIMEOUT(download.snapshot().status,DownloadStatus::Verified,1000);
        QVERIFY(download.snapshot().selection);
        QCOMPARE(download.snapshot().selection->signedEnvelope,exact.toStdString());
        checked.release.reset();
        QCOMPARE(download.snapshot().selection->signedEnvelope,exact.toStdString());
        download.invalidate();
        QVERIFY(!download.snapshot().selection);
    }
    void expiryDuringDownloadAndChannelChange() {
        for(bool channelChange:{false,true}) {
            QTemporaryDir root; qint64 now=1800000000;
            auto* network=new ScriptedNetworkManager; network->scripts={response()};
            UpdateDownloadService service(configuration(),installedRelease(),root.path(),[&]{return now;},nullptr,[&]{return network;});
            bool changed=false;
            connect(&service,&UpdateDownloadService::snapshotChanged,this,[&]{
                if(changed || service.snapshot().received==0) return; changed=true;
                if(channelChange) service.requestDownload(available("package",2,Channel::Preview));
                else now=1800003600;
            });
            service.requestDownload(available());
            QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Unavailable,1000);
            QVERIFY(service.snapshot().verifiedPath.isEmpty()); QCOMPARE(network->requests.size(),1);
        }
    }
    // Removing the pre-start recheck or checking duplicate identity before time must fail.
    void expiryBeforeRequestAndDuplicate() {
        for(bool duplicate:{false,true}) {
            QTemporaryDir root; const auto cache=root.filePath("cache"); qint64 now=1800000000;
            auto* network=new ScriptedNetworkManager; auto hanging=response(); hanging.hang=true;
            network->scripts={hanging};
            UpdateDownloadService service(configuration(),installedRelease(),cache,[&]{return now;},nullptr,[&]{return network;});
            if(!duplicate) connect(&service,&UpdateDownloadService::snapshotChanged,this,[&]{
                if(service.snapshot().status==DownloadStatus::Downloading) now=1800003600;
            });
            const auto check=available(); service.requestDownload(check);
            if(duplicate) { now=1800003600; service.requestDownload(check); }
            QCOMPARE(service.snapshot().status,DownloadStatus::Unavailable);
            QVERIFY(service.snapshot().verifiedPath.isEmpty()); QCOMPARE(network->requests.size(),duplicate?1:0);
            if(!duplicate) QVERIFY(!QFileInfo::exists(cache));
        }
    }
    void hashFailureNeverPublishesVerified() {
        QTemporaryDir root; auto* network=new ScriptedNetworkManager;
        network->scripts={response("corrupt"),response()};
        UpdateDownloadService service(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestDownload(available());
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Failed,1000);
        QCOMPARE(service.snapshot().error,std::optional<DownloadError>(DownloadError::HashMismatch));
        QVERIFY(service.snapshot().verifiedPath.isEmpty());
        QVERIFY(QDir(root.path()).entryList({"*.package"},QDir::Files).isEmpty());
        service.requestDownload(available());
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Verified,1000);
    }
    void failuresAndRetry_data() {
        QTest::addColumn<bool>("busy"); QTest::newRow("http") << false; QTest::newRow("lock-busy") << true;
    }
    void failuresAndRetry() {
        QFETCH(bool,busy); QTemporaryDir root;
        auto check=available(); PackageCache held(root.path(),check.release->manifest().artifacts[0]);
        auto* network=new ScriptedNetworkManager;
        if(busy) QCOMPARE(held.begin(),CacheError::None);
        else { auto bad=response(); bad.status=404; network->scripts.push_back(bad); }
        network->scripts.push_back(response());
        UpdateDownloadService service(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestDownload(check);
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Failed,1000);
        QVERIFY(service.snapshot().error); QCOMPARE(*service.snapshot().error,busy?DownloadError::Busy:DownloadError::Http);
        QVERIFY(service.snapshot().verifiedPath.isEmpty()); held.cancel();
        service.requestDownload(check);
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,DownloadStatus::Verified,1000);
        QVERIFY(!service.snapshot().error);
    }
    // Cancel/destruction in synchronous notifications must not resume the old operation.
    void reentry_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<int>("action");
        for(int phase:{0,1,2,3}) for(int action:{0,1,2})
            QTest::newRow(qPrintable(QString("%1-%2").arg(phase).arg(action))) << phase << action;
    }
    void reentry() {
        QFETCH(int,phase); QFETCH(int,action); QTemporaryDir root;
        auto* network=new ScriptedNetworkManager; auto script=response(); if(phase==3) script.status=404;
        network->scripts={script,response()}; QPointer<ScriptedNetworkManager> transport(network);
        QPointer<UpdateDownloadService> service=new UpdateDownloadService(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
        bool handled=false; int verified=0;
        connect(service,&UpdateDownloadService::snapshotChanged,this,[&]{
            const auto snap=service->snapshot(); if(snap.status==DownloadStatus::Verified) ++verified;
            bool trigger=phase==0 ? snap.status==DownloadStatus::Downloading && !snap.received
                : phase==1 ? snap.status==DownloadStatus::Downloading && snap.received>0
                : phase==2 ? snap.status==DownloadStatus::Verified : snap.status==DownloadStatus::Failed;
            if(handled || !trigger) return; handled=true;
            if(action==0) { if(phase<2) service->cancel(); else service->invalidate(); }
            if(action==1) delete service.data();
            if(action==2) { service->invalidate(); service->requestDownload(available()); }
        });
        service->requestDownload(available()); QTRY_VERIFY_WITH_TIMEOUT(handled,1000);
        if(action==1) { QVERIFY(!service); QVERIFY(!transport); }
        else if(action==2) QTRY_COMPARE_WITH_TIMEOUT(service->snapshot().status,DownloadStatus::Verified,1000);
        else QCOMPARE(service->snapshot().status,phase<2?DownloadStatus::Cancelled:DownloadStatus::Idle);
        if(service) delete service.data(); QTest::qWait(20);
        QCOMPARE(verified,(phase==2?1:0)+(action==2?1:0));
    }
    void cancellationAndDestructionReleaseCache() {
        for(bool destroy:{false,true}) {
            QTemporaryDir root; auto check=available(); auto* network=new ScriptedNetworkManager;
            auto hanging=response(); hanging.hang=true; network->scripts={hanging};
            auto* service=new UpdateDownloadService(configuration(),installedRelease(),root.path(),[]{return 1800000000;},nullptr,[&]{return network;});
            int notifications=0; connect(service,&UpdateDownloadService::snapshotChanged,this,[&]{++notifications;});
            service->requestDownload(check); QCOMPARE(network->requests.size(),1);
            if(destroy) delete service;
            else { service->cancel(); service->cancel(); QCOMPARE(service->snapshot().status,DownloadStatus::Cancelled); QVERIFY(service->snapshot().verifiedPath.isEmpty()); delete service; }
            const int expected=notifications; QTest::qWait(20); QCOMPARE(notifications,expected);
            PackageCache replacement(root.path(),check.release->manifest().artifacts[0]); QCOMPARE(replacement.begin(),CacheError::None);
        }
    }
    void cachePathsAreEnvironmentSeparatedAndDoNotCreateDirectories() {
        const auto base=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QCOMPARE(updateCachePath(TrustEnvironment::Test),QDir(base).filePath("updates/test/packages-v1"));
        QVERIFY(updateCachePath(TrustEnvironment::Production).isEmpty());
        QStandardPaths::setTestModeEnabled(false);
        const auto prod=updateCachePath(); const auto test=updateCachePath(TrustEnvironment::Test);
        const auto expected=QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("updates/production/packages-v1");
        QStandardPaths::setTestModeEnabled(true);
        QCOMPARE(prod,expected); QVERIFY(test.isEmpty());
    }
};
QTEST_GUILESS_MAIN(DownloadServiceTest)
#include "updatedownloadservicetest.moc"
