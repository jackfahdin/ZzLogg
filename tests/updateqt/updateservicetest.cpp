#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QLockFile>
#include "zzlogg/updateqt/updateservice.h"
#include "../update/fixturehelper.h"
#include "scriptednetwork.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace zzlogg::updateqt;
namespace {
FeedConfiguration configuration() {
    const auto context=update_fixture::context();
    return {"https://updates.example.invalid/stable","https://updates.example.invalid/preview",
        context.keys,context.allowedHosts,context.buildTime,context.environment};
}
NetworkScript signedScript(nlohmann::json payload=update_fixture::payload()) {
    NetworkScript result; result.body=QByteArray::fromStdString(update_fixture::envelope(payload.dump())); return result;
}
}
class ServiceTest : public QObject {
    Q_OBJECT
private slots:
    void informationalVersionComparisonDoesNotGrantInstallAuthority() {
      for (const auto channel : {Channel::Stable, Channel::Preview}) {
        for (unsigned patch : {0u, 1u, 2u}) {
            QTemporaryDir directory;
            auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
            auto* network=new ScriptedNetworkManager;
            auto payload=update_fixture::payload();
            payload["version"]="26.09.01";
            payload["channel"]=channel==Channel::Stable ? "stable" : "preview";
            network->scripts.push_back(signedScript(payload));
            UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
            service.setDisplayVersion(zzlogg::update::Version{26,9,patch});
            service.requestCheck(channel,CheckOrigin::Background);
            QTRY_VERIFY(service.snapshot().release.has_value());
            QCOMPARE(service.snapshot().status, patch==0 ? CheckStatus::ManualUpdateAvailable
                : channel==Channel::Stable ? CheckStatus::UpToDate : CheckStatus::ReleaseInformation);
            QCOMPARE(service.snapshot().presentToUser, patch==0);
            QVERIFY(!service.snapshot().decision.has_value());
            QVERIFY(store->read(channel).value->accepted.has_value());
        }
      }
    }
    void verifiedReleasePersistsBeforeNotification() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        bool persisted=false;
        connect(&service,&UpdateService::snapshotChanged,this,[&]{
            if(service.snapshot().status==CheckStatus::ReleaseInformation)
                persisted=store->read(Channel::Stable).value->accepted.has_value();
        });
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(service.snapshot().status,CheckStatus::ReleaseInformation,1000);
        QVERIFY(service.snapshot().release); QVERIFY(!service.snapshot().decision);
        QVERIFY(service.snapshot().presentToUser); QVERIFY(persisted);
    }
    void unconfiguredDoesNotRequestOrWrite() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager;
        UpdateService service({},store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        QCOMPARE(service.snapshot().status,CheckStatus::NotConfigured);
        QVERIFY(!service.snapshot().presentToUser);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QVERIFY(service.snapshot().presentToUser);
        QVERIFY(network->requests.isEmpty()); QVERIFY(!QFile::exists(directory.filePath("state.json")));
        const auto production=productionFeedConfiguration();
        QCOMPARE(production.environment,zzlogg::update::TrustEnvironment::Production);
        for(const auto& key:production.keys) QCOMPARE(key.purpose,zzlogg::update::KeyPurpose::Production);
    }
    void manualJoinsBackgroundAndBypassesSchedule() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager; network->scripts={signedScript(),signedScript()};
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QCOMPARE(network->requests.size(),1);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QVERIFY(service.snapshot().presentToUser);
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        QCOMPARE(network->requests.size(),1);
        service.setAutomaticChecking(false);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QCOMPARE(network->requests.size(),2);
    }
    void decisions_data() {
        QTest::addColumn<int>("scenario"); QTest::addColumn<int>("expected");
        QTest::newRow("available") << 0 << int(CheckStatus::Available);
        QTest::newRow("up-to-date") << 1 << int(CheckStatus::UpToDate);
        QTest::newRow("unsupported") << 2 << int(CheckStatus::Unsupported);
        QTest::newRow("development") << 3 << int(CheckStatus::ReleaseInformation);
    }
    void decisions() {
        QFETCH(int,scenario); QFETCH(int,expected);
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        zzlogg::update::InstalledRelease installed;
        installed.version={26,9,0}; installed.releaseSequence=1;
        installed.osVersion={10,0,22631}; installed.dataSchema=1;
        if(scenario==1) { installed.version={26,10,0}; installed.releaseSequence=2; }
        if(scenario==2) installed.arch="arm64";
        if(scenario==3) installed.developmentBuild=true;
        UpdateService service(configuration(),store,installed,[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus(expected));
        QVERIFY(store->read(Channel::Stable).value->accepted);
        if(scenario==3) QVERIFY(!service.snapshot().decision);
    }
    void invalidSignatureDoesNotAdvance() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager;
        auto script=signedScript();
        auto envelope=nlohmann::json::parse(script.body.toStdString());
        envelope["signature"]=zzlogg::update::detail::encodeBase64(std::string(64,'x'));
        script.body=QByteArray::fromStdString(envelope.dump()); network->scripts.push_back(script);
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::VerificationFailed);
        const auto state=store->read(Channel::Stable);
        QVERIFY(!state.value->accepted); QCOMPARE(state.value->failureCount,1u);
    }
    void concurrentAcceptanceRejectsOldRelease() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QCOMPARE(UpdateStateStore(directory.filePath("state.json")).accept(Channel::Stable,{3,{}},1800000000),StateError::None);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::VerificationFailed);
        QCOMPARE(store->read(Channel::Stable).value->accepted->sequence,std::uint64_t(3));
        QVERIFY(!service.snapshot().release);
    }
    void cancellationAndSwitchDiscardOldResponse() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager;
        auto preview=update_fixture::payload(); preview["channel"]="preview";
        network->scripts={signedScript(),signedScript(preview),signedScript()};
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        service.setChannel(Channel::Preview);
        QCOMPARE(store->read(Channel::Stable).value->nextAttempt,qint64(1800000900));
        service.requestCheck(Channel::Preview,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QVERIFY(!store->read(Channel::Stable).value->accepted);
        QVERIFY(store->read(Channel::Preview).value->accepted);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual); service.cancel();
        QCOMPARE(service.snapshot().status,CheckStatus::Cancelled);
        QTest::qWait(50);
        QVERIFY(!store->read(Channel::Stable).value->accepted);
        QCOMPARE(store->read(Channel::Stable).value->failureCount,0u);
        QCOMPARE(store->read(Channel::Stable).value->nextAttempt,qint64(1800000900));
    }
    void skipOnlySuppressesBackground() {
        QTemporaryDir directory; qint64 now=1800000000;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager;
        auto payload=update_fixture::payload(); payload["expiresAt"]=1800300000;
        auto next=payload; next["metadataSequence"]="3"; next["releaseSequence"]="3";
        network->scripts={signedScript(payload),signedScript(payload),signedScript(payload),signedScript(next)};
        UpdateService service(configuration(),store,{},[&]{return now;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        service.skipCurrentRelease(); QVERIFY(!service.snapshot().presentToUser);
        now+=86400;
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QVERIFY(!service.snapshot().presentToUser);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QVERIFY(service.snapshot().presentToUser);
        now+=86400;
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        QVERIFY(service.snapshot().presentToUser);
    }
    void stateErrorsPreventSuccess() {
        for(int scenario:{0,1,2}) {
            QTemporaryDir directory; const auto path=directory.filePath("state.json");
            auto store=std::make_shared<UpdateStateStore>(path);
            auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
            UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
            if(scenario==0) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("broken"); }
            service.requestCheck(Channel::Stable,CheckOrigin::Manual);
            QLockFile lock(path+".lock");
            if(scenario==1) QVERIFY(lock.tryLock(0));
            if(scenario==2) QVERIFY(QDir().mkdir(path));
            QTRY_COMPARE(service.snapshot().status,scenario==1 ? CheckStatus::StateBusy : CheckStatus::StateInvalid);
            QVERIFY(!service.snapshot().release);
            if(scenario==0) QCOMPARE(network->requests.size(),0);
        }
    }
    void checkingCallbackCanCancelOrDestroy() {
        for(bool destroy:{false,true}) {
            QTemporaryDir directory;
            auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
            auto* network=new ScriptedNetworkManager;
            QPointer<UpdateService> service=new UpdateService(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
            connect(service,&UpdateService::snapshotChanged,this,[&]{
                if(service->snapshot().status==CheckStatus::Checking) {
                    if(destroy) delete service.data(); else service->cancel();
                }
            });
            service->requestCheck(Channel::Stable,CheckOrigin::Manual);
            if(destroy) QVERIFY(!service);
            else { QCOMPARE(service->snapshot().status,CheckStatus::Cancelled); QVERIFY(network->requests.isEmpty()); delete service.data(); }
        }
    }
    void failureUsesFreshClockAndBackoff() {
        QTemporaryDir directory; qint64 now=1800000000;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager;
        NetworkScript networkError; networkError.error=QNetworkReply::RemoteHostClosedError;
        network->scripts={networkError,signedScript()};
        UpdateService service(configuration(),store,{},[&]{return now;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Background);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::NetworkError);
        QVERIFY(!service.snapshot().presentToUser);
        QCOMPARE(store->read(Channel::Stable).value->nextAttempt,now+900);
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        now+=4000; // The response has expired while the request was in flight.
        QTRY_COMPARE(service.snapshot().status,CheckStatus::VerificationFailed);
        QVERIFY(!store->read(Channel::Stable).value->accepted);
        QCOMPARE(store->read(Channel::Stable).value->nextAttempt,now+3600);
    }
    void notDueChannelCannotReuseOtherRelease() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        QCOMPARE(store->recordFailure(Channel::Preview,1800000000),StateError::None);
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::ReleaseInformation);
        service.requestCheck(Channel::Preview,CheckOrigin::Background);
        QCOMPARE(service.snapshot().channel,Channel::Preview);
        QVERIFY(!service.snapshot().release);
        service.skipCurrentRelease();
        QVERIFY(!store->read(Channel::Preview).value->skippedReleaseSequence);
    }
    void saveFailureNeverPublishesRelease() {
#ifdef Q_OS_WIN
        QTemporaryDir directory; const auto path=directory.filePath("state.json");
        auto store=std::make_shared<UpdateStateStore>(path);
        QCOMPARE(store->accept(Channel::Stable,{1,{}},1800000000),StateError::None);
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        UpdateService service(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(held!=INVALID_HANDLE_VALUE);
        struct Closer { HANDLE handle; ~Closer(){CloseHandle(handle);} } closer{held};
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QTRY_COMPARE(service.snapshot().status,CheckStatus::StateWriteFailed);
        QVERIFY(!service.snapshot().release);
        QCOMPARE(store->read(Channel::Stable).value->accepted->sequence,std::uint64_t(1));
#else
        QSKIP("Windows deny-delete handle test");
#endif
    }
    void destructionAfterRequestDoesNotNotifyOrPersist() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        auto* network=new ScriptedNetworkManager; network->scripts.push_back(signedScript());
        auto* service=new UpdateService(configuration(),store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        int callbacks=0;
        connect(service,&UpdateService::snapshotChanged,this,[&]{++callbacks;});
        service->requestCheck(Channel::Stable,CheckOrigin::Manual);
        QCOMPARE(network->requests.size(),1); QCOMPARE(callbacks,1);
        delete service;
        QTest::qWait(50);
        QCOMPARE(callbacks,1); QVERIFY(!QFile::exists(directory.filePath("state.json")));
    }
};
QTEST_GUILESS_MAIN(ServiceTest)
#include "updateservicetest.moc"
