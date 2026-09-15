#include <QtTest>
#include "fixturehelper.h"
#include "zzlogg/update/policy.h"
using namespace zzlogg::update;
using namespace update_fixture;
namespace {
VerificationResult verify(nlohmann::json input, VerificationContext ctx=context()) {
    return verifyManifest(envelope(input.dump()),ctx);
}
InstalledRelease installed() {
    InstalledRelease value;
    value.version=*parseVersion("26.09.00"); value.releaseSequence=1;
    value.osVersion={10,0,22621}; value.dataSchema=1;
    return value;
}
}
class PolicyTest : public QObject {
    Q_OBJECT
private slots:
    void accepted() {
        const auto result=verify(payload());
        QVERIFY(result.value);
        QCOMPARE(result.error,VerificationError::None);
        QCOMPARE(result.value->manifest().artifacts[0].size,std::uint64_t(1024));
        QCOMPARE(result.value->acceptedMetadata().sequence,std::uint64_t(2));
        const auto decision=selectUpdate(*result.value,installed());
        QCOMPARE(decision.status,DecisionStatus::Available);
        QVERIFY(decision.artifact);
        QCOMPARE(decision.artifact->url,std::string("https://updates.example.invalid/ZzLogg.zip"));
    }
    void invalidFields() {
        const auto good=payload();
        std::vector<nlohmann::json> bad;
        for(const auto& key:{"schema","product","metadataSequence","issuedAt","expiresAt","channel",
                            "releaseSequence","version","minUpdaterProtocol","minDataSchema",
                            "maxDataSchema","notes","artifacts"}) {
            auto p=good; p.erase(key); bad.push_back(p);
        }
        for(const auto& key:{"metadataSequence","releaseSequence"}) {
            for(const auto& value:nlohmann::json::array({"0","01","-1","+1","1.0","18446744073709551616",2,nullptr})) {
                auto p=good; p[key]=value; bad.push_back(p);
            }
        }
        for(const auto& key:{"schema","issuedAt","expiresAt","minUpdaterProtocol","minDataSchema","maxDataSchema"}) {
            for(const auto& value:nlohmann::json::array({-1,1.5,"1",nullptr})) {
                auto p=good; p[key]=value; bad.push_back(p);
            }
        }
        auto p=good; p["product"]="other"; bad.push_back(p);
        p=good; p["extra"]=true; bad.push_back(p);
        p=good; p["schema"]=2; bad.push_back(p);
        p=good; p["version"]="26.9.00"; bad.push_back(p);
        p=good; p["minDataSchema"]=2; bad.push_back(p);
        p=good; p["maxDataSchema"]=4294967296ULL; bad.push_back(p);
        p=good; p["minUpdaterProtocol"]=0; bad.push_back(p);
        p=good; p["issuedAt"]=9007199254740992ULL; bad.push_back(p);
        p=good; p["notes"]["en"]=std::string(16*1024+1,'a'); bad.push_back(p);
        p=good; p["notes"]["fr"]="x"; bad.push_back(p);
        p=good; p["artifacts"]=nlohmann::json::array(); bad.push_back(p);
        p=good; p["artifacts"].push_back(p["artifacts"][0]); bad.push_back(p);
        for(const auto& value:bad) {
            const auto result=verify(value);
            QVERIFY2(!result.value,value.dump().c_str());
            QVERIFY(result.error!=VerificationError::None);
        }
        p=good; p["metadataSequence"]="18446744073709551615";
        QVERIFY(verify(p).value);
    }
    void artifactConstraints() {
        const auto good=payload();
        for(const auto& key:{"os","arch","distribution","format","minOsVersion","url","size","sha256"}) {
            auto p=good; p["artifacts"][0].erase(key); QVERIFY(!verify(p).value);
            p=good; p["artifacts"][0][key]=nullptr; QVERIFY(!verify(p).value);
        }
        const std::vector<std::pair<std::string,nlohmann::json>> values{
            {"size","0"},{"size","01"},{"size","536870913"},{"size",1024},
            {"sha256",std::string(64,'A')},{"sha256","abc"},{"format","exe"},
            {"distribution","other"},{"minOsVersion","10.0"},{"minOsVersion","10.0.-1"},
            {"minOsVersion","10.0.4294967296"},{"os",""},{"arch",""}};
        for(const auto& item:values) {
            auto p=good; p["artifacts"][0][item.first]=item.second;
            QVERIFY2(!verify(p).value,p.dump().c_str());
        }
        auto p=good; p["artifacts"][0]["size"]="536870912"; QVERIFY(verify(p).value);
        p=good; p["artifacts"][0]["os"]="linux"; QVERIFY(verify(p).value);
        p=good; p["artifacts"][0]["extra"]=1; QVERIFY(!verify(p).value);
        p=good;
        for(int i=1;i<17;++i) {
            auto a=good["artifacts"][0]; a["arch"]="arch"+std::to_string(i);
            p["artifacts"].push_back(a);
        }
        QVERIFY(!verify(p).value);
    }
    void urls() {
        for(const auto* url:{"http://updates.example.invalid/a","https://user@updates.example.invalid/a",
            "https://updates.example.invalid/a#x","https://updates.example.invalid/a\r\nX",
            "https://updates.example.invalid\\a","https://updates.example.invalid/%zz",
            "https://updates.example.invalid:444/a","https://updates.example.invalid.evil/a",
            "https://evilupdates.example.invalid/a","https://updates.example.invalid./a",
            "https://updates.example.invalid/a b","https://updates.example.invalid/%0d",
            "https:///a","https://updates.example.invalid:443:443/a"}) {
            auto p=payload(); p["artifacts"][0]["url"]=url;
            QVERIFY2(!verify(p).value,url);
        }
        for(const auto* url:{"https://updates.example.invalid:443/a?x=1&y=%20",
                            "https://UPDATES.EXAMPLE.INVALID/a"}) {
            auto p=payload(); p["artifacts"][0]["url"]=url; QVERIFY(verify(p).value);
        }
    }
    void timeAndReplay() {
        const auto good=payload(); auto ctx=context();
        auto result=verify(good,ctx); QVERIFY(result.value);
        ctx.lastAccepted=result.value->acceptedMetadata();
        QVERIFY(verify(good,ctx).value);
        auto alias=ctx.keys.front(); alias.id="rotation";
        ctx.keys.push_back(alias);
        const auto rotated=verifyManifest(envelope(good.dump(),"rotation"),ctx);
        QVERIFY(rotated.value);
        QVERIFY(rotated.value->acceptedMetadata().payloadDigest==ctx.lastAccepted->payloadDigest);
        auto p=good; p["notes"]["en"]="changed";
        QCOMPARE(verify(p,ctx).error,VerificationError::MetadataConflict);
        p=good; p["metadataSequence"]="1";
        QCOMPARE(verify(p,ctx).error,VerificationError::Replay);
        // Semantically identical but different signed bytes must also conflict.
        QCOMPARE(verifyManifest(envelope(good.dump()+" "),ctx).error,VerificationError::MetadataConflict);
        ctx=context(); ctx.now=1800003600;
        QCOMPARE(verify(good,ctx).error,VerificationError::Expired);
        ctx.now=1800003599; QVERIFY(verify(good,ctx).value);
        ctx=context(); p=good; p["issuedAt"]=ctx.now+300; QVERIFY(verify(p,ctx).value);
        p["issuedAt"]=ctx.now+301; QCOMPARE(verify(p,ctx).error,VerificationError::ClockInvalid);
        p=good; p["expiresAt"]=1799999900; QVERIFY(!verify(p).value);
        p=good; p["expiresAt"]=1799999900+30*86400; QVERIFY(verify(p).value);
        p["expiresAt"]=1799999900+30*86400+1; QVERIFY(!verify(p).value);
        ctx=context(); ctx.buildTime=ctx.now+86401;
        QCOMPARE(verify(good,ctx).error,VerificationError::ClockInvalid);
        ctx.buildTime=ctx.now+86400; QVERIFY(verify(good,ctx).value);
        ctx.now=INT64_MIN; QVERIFY(!verify(good,ctx).value);
        ctx=context(); ctx.now=INT64_MAX; QVERIFY(!verify(good,ctx).value);
        ctx=context(); ctx.channel="preview";
        QCOMPARE(verify(good,ctx).error,VerificationError::ChannelMismatch);
        p=good; p["channel"]="preview"; QVERIFY(verify(p,ctx).value);
        p=good; p["artifacts"][0]["arch"]="arm64"; result=verify(p);
        QVERIFY(result.value); QCOMPARE(result.value->acceptedMetadata().sequence,std::uint64_t(2));
    }
    void selection() {
        auto result=verify(payload()); QVERIFY(result.value);
        const auto& verified=*result.value;
        auto current=installed(); current.distribution=Distribution::Installer;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::NoCompatibleArtifact);
        current=installed(); current.developmentBuild=true;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::DevelopmentBuild);
        current=installed(); current.channel="preview";
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::ChannelMismatch);
        current=installed(); current.updaterProtocol=0;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::ProtocolUnsupported);
        current=installed(); current.dataSchema=2;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::DataIncompatible);
        current=installed(); current.releaseSequence=2;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::ReleaseConflict);
        current.version=*parseVersion("26.10.00");
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::NoUpdate);
        current.releaseSequence=1;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::ReleaseConflict);
        current.releaseSequence=3;
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::NoUpdate);
        current=installed(); current.osVersion={10,0,19040};
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::OsUnsupported);
        current.osVersion={10,0,19041};
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::Available);
        current.osVersion={9,99,99999};
        QCOMPARE(selectUpdate(verified,current).status,DecisionStatus::OsUnsupported);
        current=installed(); current.arch="arm64";
        const auto decision=selectUpdate(verified,current);
        QCOMPARE(decision.status,DecisionStatus::NoCompatibleArtifact);
        QVERIFY(!decision.artifact);
        auto p=payload(); p["artifacts"][0]["distribution"]="installer";
        p["artifacts"][0]["format"]="nsis-exe";
        result=verify(p); QVERIFY(result.value);
        current=installed(); current.distribution=Distribution::Installer;
        QCOMPARE(selectUpdate(*result.value,current).status,DecisionStatus::Available);
    }
    void selectsMatchingArtifactRegardlessOfOrder() {
        auto p=payload();
        auto installer=p["artifacts"][0];
        installer["distribution"]="installer"; installer["format"]="nsis-exe";
        installer["url"]="https://updates.example.invalid/setup.exe";
        auto arm=p["artifacts"][0]; arm["arch"]="arm64";
        p["artifacts"].push_back(installer); p["artifacts"].push_back(arm);
        auto current=installed(); current.distribution=Distribution::Installer;
        for(int i=0;i<3;++i) {
            const auto result=verify(p); QVERIFY(result.value);
            const auto decision=selectUpdate(*result.value,current);
            QCOMPARE(decision.status,DecisionStatus::Available); QVERIFY(decision.artifact);
            QCOMPARE(decision.artifact->url,std::string("https://updates.example.invalid/setup.exe"));
            std::rotate(p["artifacts"].begin(),p["artifacts"].begin()+1,p["artifacts"].end());
        }
    }
    void rejectedInputsNeverCarryAcceptance() {
        const auto good=payload();
        const auto accepted=verify(good); QVERIFY(accepted.value);
        std::vector<VerificationContext> contexts;
        auto ctx=context(); ctx.now=1800003600; contexts.push_back(ctx);
        ctx=context(); ctx.buildTime=ctx.now+86401; contexts.push_back(ctx);
        ctx=context(); ctx.lastAccepted=accepted.value->acceptedMetadata();
        ctx.lastAccepted->sequence=3; contexts.push_back(ctx);
        ctx.lastAccepted->sequence=2; ctx.lastAccepted->payloadDigest[0]^=1; contexts.push_back(ctx);
        for(const auto& input:contexts) {
            const auto result=verify(good,input);
            QVERIFY(!result.value); QVERIFY(result.error!=VerificationError::None);
        }
        // Wrong signature must be rejected before malformed payload parsing.
        auto wire=nlohmann::json::parse(envelope("not json"));
        wire["signature"]=zzlogg::update::detail::encodeBase64(std::string(64,0));
        const auto result=verifyManifest(wire.dump(),context());
        QCOMPARE(result.error,VerificationError::SignatureInvalid); QVERIFY(!result.value);
    }
};
QTEST_GUILESS_MAIN(PolicyTest)
#include "policytest.moc"
