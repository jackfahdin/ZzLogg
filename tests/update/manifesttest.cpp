#include <QtTest>
#include "envelope.h"
#include "fixturehelper.h"
using namespace zzlogg::update;
using namespace zzlogg::update::detail;
class ManifestTest : public QObject {
    Q_OBJECT
private slots:
    void acceptsOriginalBytes() {
        const auto input=update_fixture::envelope(" { \"schema\" : 1 } ");
        const auto result=verifyEnvelope(input,update_fixture::context());
        QVERIFY(result.payload);
        QCOMPARE(*result.payload,std::string(" { \"schema\" : 1 } "));
        QCOMPARE(result.error,VerificationError::None);
        QVERIFY(verifyEnvelope(update_fixture::envelope("{} "),update_fixture::context()).payload);
    }
    void rejectsMutations() {
        const auto original=nlohmann::json::parse(update_fixture::envelope("{}"));
        const auto context=update_fixture::context();
        std::vector<nlohmann::json> cases;
        auto changed=original; changed["schema"]=1.0; cases.push_back(changed);
        changed=original; changed["schema"]=2; cases.push_back(changed);
        changed=original; changed["keyId"]="unknown"; cases.push_back(changed);
        changed=original; changed["keyId"]="bad id"; cases.push_back(changed);
        changed=original; changed["extra"]=1; cases.push_back(changed);
        changed=original; changed.erase("signature"); cases.push_back(changed);
        changed=original; changed["payload"]=encodeBase64("{} "); cases.push_back(changed);
        changed=original; changed["signature"]=encodeBase64(std::string(64,0)); cases.push_back(changed);
        changed=original; changed["signature"]="AA=="; cases.push_back(changed);
        changed=original; changed["payload"]="e31="; cases.push_back(changed);
        for(const auto& input:cases) QVERIFY(!verifyEnvelope(input.dump(),context).payload);
        QVERIFY(!verifyEnvelope(update_fixture::envelope("{}","fixture","wrong\n"),context).payload);
        auto duplicate=original.dump();
        duplicate.insert(1,"\"schema\":1,");
        QVERIFY(!verifyEnvelope(duplicate,context).payload);
        QVERIFY(!verifyEnvelope(std::string(256*1024+1,' '),context).payload);
        QVERIFY(!verifyEnvelope(update_fixture::envelope(std::string(128*1024+1,'x')),context).payload);
    }
    void trustBoundaries() {
        const auto input=update_fixture::envelope("{}");
        auto context=update_fixture::context();
        context.keys.push_back(context.keys.front());
        QVERIFY(!verifyEnvelope(input,context).payload);
        context=update_fixture::context(); context.keys.clear();
        QVERIFY(!verifyEnvelope(input,context).payload);
        context=update_fixture::context(); context.keys[0].publicKey.pop_back();
        QVERIFY(!verifyEnvelope(input,context).payload);
        context=update_fixture::context(); context.environment=TrustEnvironment::Production;
        QVERIFY(!verifyEnvelope(input,context).payload);
        context.keys[0].purpose=KeyPurpose::Production;
        QVERIFY(!verifyEnvelope(input,context).payload);
        context.keys.clear();
        QVERIFY(!verifyEnvelope(input,context).payload);
    }
    void bindsTrustedKeyId() {
        auto context=update_fixture::context();
        auto alias=context.keys.front();
        alias.id="alias";
        context.keys.push_back(alias);
        auto input=nlohmann::json::parse(update_fixture::envelope("{}"));
        input["keyId"]="alias";
        QCOMPARE(verifyEnvelope(input.dump(),context).error,VerificationError::SignatureInvalid);
        QVERIFY(verifyEnvelope(update_fixture::envelope("{}","alias"),context).payload);
    }
    void productionRejectsTestPurposeIndependently() {
        auto context=update_fixture::context();
        context.environment=TrustEnvironment::Production;
        // Not a blocked fixture key: checks purpose before any signature attempt.
        context.keys[0].publicKey[0]^=1;
        const auto input=update_fixture::envelope("{}");
        QCOMPARE(verifyEnvelope(input,context).error,VerificationError::TrustInvalid);
        context.keys[0].purpose=KeyPurpose::Production;
        QCOMPARE(verifyEnvelope(input,context).error,VerificationError::SignatureInvalid);
    }
};
QTEST_GUILESS_MAIN(ManifestTest)
#include "manifesttest.moc"
