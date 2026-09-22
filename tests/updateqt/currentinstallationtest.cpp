#include <QtTest>

#include "zzlogg/updateqt/currentinstallation.h"

using namespace zzlogg::update;
using namespace zzlogg::updateqt;

namespace {

ReleaseIdentity official()
{
    return {*parseVersion("26.09.03"), 260903, "stable", "windows", "x64", 0, 1};
}

InstallationIdentity registeredInstallation()
{
    return {InstallationKind::Registered, QStringLiteral("C:\\Program Files\\ZzLogg")};
}

} // namespace

class CurrentInstallationTest : public QObject {
    Q_OBJECT

private slots:
    // 三项输入缺一不可：非官方构建、非注册安装、系统版本探测失败，
    // 任意一项都必须失败关闭，而不是构造出一个半真的已安装身份。
    void composesOnlyWithEveryInput()
    {
        const InstallationIdentity unregistered{InstallationKind::Unregistered, {}};

        QVERIFY(!composeInstalledRelease(official(), unregistered, {10, 0, 22631}));
        QVERIFY(!composeInstalledRelease(std::nullopt, registeredInstallation(), {10, 0, 22631}));
        QVERIFY(!composeInstalledRelease(official(), registeredInstallation(), {}));

        const auto composed
            = composeInstalledRelease(official(), registeredInstallation(), {10, 0, 22631});

        QVERIFY(composed);
        QCOMPARE(composed->version.year, 26u);
        QCOMPARE(composed->version.month, 9u);
        QCOMPARE(composed->version.patch, 3u);
        QCOMPARE(composed->releaseSequence, std::uint64_t{260903});
        QCOMPARE(composed->channel, std::string{"stable"});
        QCOMPARE(composed->distribution, Distribution::Installer);
        QCOMPARE(composed->osVersion.patch, std::uint32_t{22631});
    }

    void nonOfficialBuildYieldsNothing()
    {
        // 本机与 PR 构建都不带官方发布身份，生产入口必须返回空。
        QVERIFY(!currentInstalledRelease());
    }
};

QTEST_GUILESS_MAIN(CurrentInstallationTest)
#include "currentinstallationtest.moc"
