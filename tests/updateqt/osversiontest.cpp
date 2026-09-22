#include <QtTest>
#include "zzlogg/updateqt/osversion.h"

using namespace zzlogg::updateqt;

class OsVersionTest : public QObject {
    Q_OBJECT
private slots:
    void reportsBuildNumberOnWindowsAndZeroElsewhere() {
        const auto version = currentOsVersion();
#ifdef Q_OS_WIN
        // 清单的 minOsVersion 是 10.0.19041，补丁位必须是真实构建号而不是 0。
        QVERIFY(version.major >= 10);
        QVERIFY(version.patch >= 10240);
#else
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
#endif
    }

    void makeOsVersionMapsValidPlatformReport() {
        const auto version = makeOsVersion(10, 0, 22631);
        QCOMPARE(version.major, 10u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 22631u);
    }

    void makeOsVersionRejectsZeroBuildNumber() {
        const auto version = makeOsVersion(10, 0, 0);
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
    }

    void makeOsVersionRejectsNegativeBuildNumber() {
        const auto version = makeOsVersion(10, 0, -1);
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
    }

    void makeOsVersionRejectsZeroMajor() {
        const auto version = makeOsVersion(0, 0, 22631);
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
    }

    void makeOsVersionRejectsNegativeMinor() {
        const auto version = makeOsVersion(10, -1, 22631);
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
    }
};
QTEST_GUILESS_MAIN(OsVersionTest)
#include "osversiontest.moc"
