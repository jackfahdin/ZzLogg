#include <QtTest>
#include "fixturehelper.h"
#include "zzlogg/update/executionselection.h"

using namespace zzlogg::update;
namespace {
nlohmann::json installerPayload() {
    auto value=update_fixture::payload();
    auto& artifact=value["artifacts"][0];
    artifact["distribution"]="installer";
    artifact["format"]="nsis-exe";
    artifact["minOsVersion"]="10.0.22631";
    artifact["url"]="https://updates.example.invalid/ZzLoggSetup.exe";
    artifact["size"]="1234";
    artifact["sha256"]=std::string(64,'1');
    return value;
}
InstalledRelease installed() {
    InstalledRelease value;
    value.version=*parseVersion("26.09.00");
    value.releaseSequence=1;
    value.channel="stable";
    value.os="windows";
    value.arch="x64";
    value.distribution=Distribution::Installer;
    value.osVersion={10,0,22631};
    value.dataSchema=1;
    value.updaterProtocol=1;
    return value;
}
struct Fixture {
    std::string envelope="\n"+update_fixture::envelope(installerPayload().dump())+"\r\n";
    VerificationContext context=update_fixture::context();
    VerifiedManifest verified=*verifyManifest(envelope,context).value;
    UpdateSelection selection=*makeUpdateSelection(verified,installed());
    Fixture() { context.lastAccepted=verified.acceptedMetadata(); }
};
void compareArtifact(const Artifact& actual,const Artifact& expected) {
    QCOMPARE(actual.os,expected.os);
    QCOMPARE(actual.arch,expected.arch);
    QCOMPARE(actual.distribution,expected.distribution);
    QCOMPARE(actual.format,expected.format);
    QCOMPARE(actual.minOsVersion.major,expected.minOsVersion.major);
    QCOMPARE(actual.minOsVersion.minor,expected.minOsVersion.minor);
    QCOMPARE(actual.minOsVersion.patch,expected.minOsVersion.patch);
    QCOMPARE(actual.url,expected.url);
    QCOMPARE(actual.size,expected.size);
    QCOMPARE(actual.sha256,expected.sha256);
}
}

class ExecutionSelectionTest : public QObject {
    Q_OBJECT
private slots:
    void createsInstallerSelectionAndRevalidatesExactTarget() {
        Fixture fixture;
        QCOMPARE(fixture.selection.signedEnvelope,fixture.envelope);
        QCOMPARE(fixture.selection.accepted.sequence,std::uint64_t(2));
        QCOMPARE(fixture.selection.releaseSequence,std::uint64_t(2));
        const auto result=revalidateUpdateSelection(fixture.selection,fixture.context,installed());
        QCOMPARE(result.error,SelectionError::None);
        QVERIFY(result.artifact);
        Artifact expected{"windows","x64",Distribution::Installer,"nsis-exe",{10,0,22631},
            "https://updates.example.invalid/ZzLoggSetup.exe",1234,std::string(64,'1')};
        compareArtifact(*result.artifact,expected);
    }

    void makeSelectionRejectsInvalidOrUnavailableInstalledRelease() {
        Fixture fixture;
        std::vector<InstalledRelease> invalid;
        auto value=installed(); value.developmentBuild=true; invalid.push_back(value);
        value=installed(); value.distribution=Distribution::Portable; invalid.push_back(value);
        value=installed(); value.os="linux"; invalid.push_back(value);
        value=installed(); value.arch="arm64"; invalid.push_back(value);
        value=installed(); value.releaseSequence=0; invalid.push_back(value);
        value=installed(); value.version.month=0; invalid.push_back(value);
        value=installed(); value.osVersion.major=0; invalid.push_back(value);
        value=installed(); value.version={26,10,0}; value.releaseSequence=2; invalid.push_back(value);
        for(const auto& current:invalid) QVERIFY(!makeUpdateSelection(fixture.verified,current));
    }

    void freshVerificationFailuresAreRejected() {
        Fixture fixture;
        auto record=fixture.selection;
        record.signedEnvelope[record.signedEnvelope.find("signature")]='S';
        QCOMPARE(revalidateUpdateSelection(record,fixture.context,installed()).error,
                 SelectionError::VerificationFailed);

        auto context=fixture.context; context.now=1800003600;
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
        context=fixture.context; context.channel="preview";
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
        context=fixture.context; context.keys.clear();
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
        context=fixture.context; context.environment=TrustEnvironment::Production;
        context.keys[0].purpose=KeyPurpose::Production;
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
    }

    void antiReplayStateMustMatchSignedEnvelope() {
        Fixture fixture;
        auto context=fixture.context; context.lastAccepted.reset();
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::StateMissing);
        context=fixture.context; ++context.lastAccepted->sequence;
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
        context=fixture.context; context.lastAccepted->payloadDigest[0]^=1;
        QCOMPARE(revalidateUpdateSelection(fixture.selection,context,installed()).error,
                 SelectionError::VerificationFailed);
    }

    void modifiedSelectionRecordIsNeverTrusted() {
        Fixture fixture;
        std::vector<UpdateSelection> changed;
        auto record=fixture.selection; ++record.accepted.sequence; changed.push_back(record);
        record=fixture.selection; record.accepted.payloadDigest[0]^=1; changed.push_back(record);
        record=fixture.selection; ++record.releaseSequence; changed.push_back(record);
        record=fixture.selection; record.artifact.os="linux"; changed.push_back(record);
        record=fixture.selection; record.artifact.arch="arm64"; changed.push_back(record);
        record=fixture.selection; record.artifact.distribution=Distribution::Portable; changed.push_back(record);
        record=fixture.selection; record.artifact.format="zip"; changed.push_back(record);
        record=fixture.selection; ++record.artifact.minOsVersion.major; changed.push_back(record);
        record=fixture.selection; ++record.artifact.minOsVersion.minor; changed.push_back(record);
        record=fixture.selection; ++record.artifact.minOsVersion.patch; changed.push_back(record);
        record=fixture.selection; record.artifact.url="https://updates.example.invalid/other.exe"; changed.push_back(record);
        record=fixture.selection; ++record.artifact.size; changed.push_back(record);
        record=fixture.selection; record.artifact.sha256[0]='2'; changed.push_back(record);
        for(const auto& input:changed) {
            const auto result=revalidateUpdateSelection(input,fixture.context,installed());
            QCOMPARE(result.error,SelectionError::TargetChanged);
            QVERIFY(!result.artifact);
        }
    }

    void changedCurrentInstallationMakesTargetUnavailable() {
        Fixture fixture;
        std::vector<InstalledRelease> changed;
        auto current=installed(); current.version={26,10,0}; current.releaseSequence=2; changed.push_back(current);
        current=installed(); current.developmentBuild=true; changed.push_back(current);
        current=installed(); current.distribution=Distribution::Portable; changed.push_back(current);
        current=installed(); current.os="linux"; changed.push_back(current);
        current=installed(); current.arch="arm64"; changed.push_back(current);
        current=installed(); current.releaseSequence=0; changed.push_back(current);
        current=installed(); current.version.month=0; changed.push_back(current);
        current=installed(); current.osVersion.major=0; changed.push_back(current);
        for(const auto& input:changed) {
            const auto result=revalidateUpdateSelection(fixture.selection,fixture.context,input);
            QCOMPARE(result.error,SelectionError::Unavailable);
            QVERIFY(!result.artifact);
        }
    }
};

QTEST_GUILESS_MAIN(ExecutionSelectionTest)
#include "executionselectiontest.moc"
