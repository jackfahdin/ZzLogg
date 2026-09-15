#include <QtTest>

#include "zzlogg/updateqt/installationidentity.h"

using namespace zzlogg::updateqt;

namespace {

InstallationEvidence registeredEvidence()
{
    InstallationEvidence evidence;
    evidence.supportedPlatform = true;
    evidence.registrationPresent = true;
    evidence.safePaths = true;
    evidence.sameDirectory = true;
    evidence.markerPresent = true;
    evidence.markerValid = true;
    evidence.identitySchema = 1;
    evidence.installRoot = QStringLiteral("C:/Program Files/ZzLogg");
    return evidence;
}

} // namespace

class InstallationIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void classifiesEvidence_data()
    {
        QTest::addColumn<InstallationEvidence>("evidence");
        QTest::addColumn<InstallationKind>("expectedKind");
        QTest::addColumn<QString>("expectedRoot");

        QTest::newRow("unregistered-portable") << InstallationEvidence{true}
            << InstallationKind::Unregistered << QString{};

        auto copied = registeredEvidence();
        copied.registrationPresent = false;
        QTest::newRow("copied-installation-directory") << copied
            << InstallationKind::Invalid << QString{};

        auto denied = registeredEvidence();
        denied.readFailed = true;
        QTest::newRow("read-denied") << denied << InstallationKind::Invalid << QString{};

        auto missingMarker = registeredEvidence();
        missingMarker.markerPresent = false;
        QTest::newRow("missing-marker") << missingMarker << InstallationKind::Invalid << QString{};

        auto malformedMarker = registeredEvidence();
        malformedMarker.markerValid = false;
        QTest::newRow("malformed-marker") << malformedMarker << InstallationKind::Invalid << QString{};

        auto mismatchedDirectory = registeredEvidence();
        mismatchedDirectory.sameDirectory = false;
        QTest::newRow("mismatched-directory") << mismatchedDirectory
            << InstallationKind::Invalid << QString{};

        auto unsafePaths = registeredEvidence();
        unsafePaths.safePaths = false;
        QTest::newRow("unsafe-paths") << unsafePaths << InstallationKind::Invalid << QString{};

        auto legacy = registeredEvidence();
        legacy.identitySchema.reset();
        QTest::newRow("missing-schema") << legacy << InstallationKind::Legacy << QString{};

        auto unknownSchema = registeredEvidence();
        unknownSchema.identitySchema = 2;
        QTest::newRow("unknown-schema") << unknownSchema << InstallationKind::Invalid << QString{};

        auto unsupported = registeredEvidence();
        unsupported.supportedPlatform = false;
        QTest::newRow("unsupported-platform") << unsupported
            << InstallationKind::Unsupported << QString{};

        QTest::newRow("registered") << registeredEvidence()
            << InstallationKind::Registered << QStringLiteral("C:/Program Files/ZzLogg");
    }

    void classifiesEvidence()
    {
        QFETCH(InstallationEvidence, evidence);
        QFETCH(InstallationKind, expectedKind);
        QFETCH(QString, expectedRoot);

        const auto identity = evaluateInstallation(evidence);
        QCOMPARE(identity.kind, expectedKind);
        QCOMPARE(identity.installRoot, expectedRoot);
    }

    void rejectsEmptyRootForOtherwiseRegisteredEvidence()
    {
        auto evidence = registeredEvidence();
        evidence.installRoot.clear();

        const auto identity = evaluateInstallation(evidence);
        QCOMPARE(identity.kind, InstallationKind::Invalid);
        QVERIFY(identity.installRoot.isEmpty());
    }
};

QTEST_GUILESS_MAIN(InstallationIdentityTest)
#include "installationidentitytest.moc"
