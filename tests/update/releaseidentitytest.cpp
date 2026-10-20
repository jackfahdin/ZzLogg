#include "zzlogg/update/releaseidentity.h"

#include <QtTest>

using namespace zzlogg::update;

class ReleaseIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void identityMatchesBuildConfiguration()
    {
#if TEST_OFFICIAL_RELEASE
        const auto release = compiledReleaseIdentity();
        QVERIFY(release);
        const auto expectedVersion = parseVersion(TEST_RELEASE_VERSION);
        QVERIFY(expectedVersion);
        QCOMPARE(release->version.year, expectedVersion->year);
        QCOMPARE(release->version.month, expectedVersion->month);
        QCOMPARE(release->version.patch, expectedVersion->patch);
        QCOMPARE(release->releaseSequence, std::uint64_t{TEST_RELEASE_SEQUENCE});
        QCOMPARE(release->channel, std::string{TEST_RELEASE_CHANNEL});
#if defined(_WIN32)
        QCOMPARE(release->os, std::string{"windows"});
#elif defined(__APPLE__)
        QCOMPARE(release->os, std::string{"macos"});
#else
        QCOMPARE(release->os, std::string{"linux"});
#endif
#if defined(__aarch64__) || defined(__arm64__)
        QCOMPARE(release->arch, std::string{"arm64"});
#else
        QCOMPARE(release->arch, std::string{"x64"});
#endif
        QCOMPARE(release->dataSchema, std::uint32_t{TEST_RELEASE_DATA_SCHEMA});
        QCOMPARE(release->updaterProtocol, std::uint32_t{1});
#else
        QVERIFY(!compiledReleaseIdentity());
#endif
    }
};

QTEST_GUILESS_MAIN(ReleaseIdentityTest)
#include "releaseidentitytest.moc"
