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
};
QTEST_GUILESS_MAIN(OsVersionTest)
#include "osversiontest.moc"
