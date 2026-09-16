#include <QtTest>

#include "zzlogg/updateqt/installedrelease.h"

#include <limits>

using namespace zzlogg::update;
using namespace zzlogg::updateqt;

namespace {

ReleaseIdentity validRelease()
{
    return {{26, 9, 0}, 123, "stable", "windows", "x64", 0, 1};
}

InstallationIdentity registeredInstallation()
{
    return {InstallationKind::Registered, QStringLiteral("C:/Program Files/ZzLogg")};
}

OsVersion validOsVersion()
{
    return {10, 0, 22631};
}

enum class InvalidCase {
    MissingRelease,
    Unregistered,
    Legacy,
    InvalidInstallation,
    UnsupportedInstallation,
    EmptyInstallRoot,
    EmptyChannel,
    UnknownChannel,
    WrongOs,
    WrongArch,
    YearOutOfRange,
    MonthZero,
    MonthOutOfRange,
    PatchOutOfRange,
    ZeroReleaseSequence,
    ProtocolZero,
    ProtocolTwo,
    OsMajorZero,
};

} // namespace

Q_DECLARE_METATYPE(InvalidCase)

class InstalledReleaseTest : public QObject {
    Q_OBJECT

private slots:
    void copiesCompleteInstallerIdentity()
    {
        const auto result = makeInstalledRelease(validRelease(), registeredInstallation(), validOsVersion());

        QVERIFY(result);
        QCOMPARE(result->version.year, 26u);
        QCOMPARE(result->version.month, 9u);
        QCOMPARE(result->version.patch, 0u);
        QCOMPARE(result->releaseSequence, std::uint64_t{123});
        QVERIFY(!result->developmentBuild);
        QCOMPARE(result->channel, std::string{"stable"});
        QCOMPARE(result->os, std::string{"windows"});
        QCOMPARE(result->arch, std::string{"x64"});
        QCOMPARE(result->distribution, Distribution::Installer);
        QCOMPARE(result->osVersion.major, std::uint32_t{10});
        QCOMPARE(result->osVersion.minor, std::uint32_t{0});
        QCOMPARE(result->osVersion.patch, std::uint32_t{22631});
        QCOMPARE(result->dataSchema, std::uint32_t{0});
        QCOMPARE(result->updaterProtocol, std::uint32_t{1});
    }

    void acceptsValidBoundaries_data()
    {
        QTest::addColumn<QByteArray>("channel");
        QTest::addColumn<qulonglong>("releaseSequence");
        QTest::addColumn<quint32>("dataSchema");

        QTest::newRow("stable-schema-zero") << QByteArray("stable") << qulonglong{1} << quint32{0};
        QTest::newRow("preview-maximums") << QByteArray("preview")
            << std::numeric_limits<qulonglong>::max() << std::numeric_limits<quint32>::max();
    }

    void acceptsValidBoundaries()
    {
        QFETCH(QByteArray, channel);
        QFETCH(qulonglong, releaseSequence);
        QFETCH(quint32, dataSchema);
        auto release = validRelease();
        release.channel = channel.toStdString();
        release.releaseSequence = releaseSequence;
        release.dataSchema = dataSchema;

        const auto result = makeInstalledRelease(release, registeredInstallation(), validOsVersion());

        QVERIFY(result);
        QCOMPARE(result->channel, channel.toStdString());
        QCOMPARE(result->releaseSequence, static_cast<std::uint64_t>(releaseSequence));
        QCOMPARE(result->dataSchema, static_cast<std::uint32_t>(dataSchema));
    }

    void rejectsInvalidInputs_data()
    {
        QTest::addColumn<InvalidCase>("invalidCase");
        QTest::newRow("missing-release") << InvalidCase::MissingRelease;
        QTest::newRow("unregistered") << InvalidCase::Unregistered;
        QTest::newRow("legacy") << InvalidCase::Legacy;
        QTest::newRow("invalid-installation") << InvalidCase::InvalidInstallation;
        QTest::newRow("unsupported-installation") << InvalidCase::UnsupportedInstallation;
        QTest::newRow("empty-install-root") << InvalidCase::EmptyInstallRoot;
        QTest::newRow("empty-channel") << InvalidCase::EmptyChannel;
        QTest::newRow("unknown-channel") << InvalidCase::UnknownChannel;
        QTest::newRow("wrong-os") << InvalidCase::WrongOs;
        QTest::newRow("wrong-arch") << InvalidCase::WrongArch;
        QTest::newRow("year-out-of-range") << InvalidCase::YearOutOfRange;
        QTest::newRow("month-zero") << InvalidCase::MonthZero;
        QTest::newRow("month-out-of-range") << InvalidCase::MonthOutOfRange;
        QTest::newRow("patch-out-of-range") << InvalidCase::PatchOutOfRange;
        QTest::newRow("zero-release-sequence") << InvalidCase::ZeroReleaseSequence;
        QTest::newRow("protocol-zero") << InvalidCase::ProtocolZero;
        QTest::newRow("protocol-two") << InvalidCase::ProtocolTwo;
        QTest::newRow("os-major-zero") << InvalidCase::OsMajorZero;
    }

    void rejectsInvalidInputs()
    {
        QFETCH(InvalidCase, invalidCase);
        std::optional<ReleaseIdentity> release = validRelease();
        auto installation = registeredInstallation();
        auto osVersion = validOsVersion();

        switch (invalidCase) {
        case InvalidCase::MissingRelease: release.reset(); break;
        case InvalidCase::Unregistered: installation.kind = InstallationKind::Unregistered; break;
        case InvalidCase::Legacy: installation.kind = InstallationKind::Legacy; break;
        case InvalidCase::InvalidInstallation: installation.kind = InstallationKind::Invalid; break;
        case InvalidCase::UnsupportedInstallation: installation.kind = InstallationKind::Unsupported; break;
        case InvalidCase::EmptyInstallRoot: installation.installRoot.clear(); break;
        case InvalidCase::EmptyChannel: release->channel.clear(); break;
        case InvalidCase::UnknownChannel: release->channel = "nightly"; break;
        case InvalidCase::WrongOs: release->os = "linux"; break;
        case InvalidCase::WrongArch: release->arch = "arm64"; break;
        case InvalidCase::YearOutOfRange: release->version.year = 100; break;
        case InvalidCase::MonthZero: release->version.month = 0; break;
        case InvalidCase::MonthOutOfRange: release->version.month = 13; break;
        case InvalidCase::PatchOutOfRange: release->version.patch = 100; break;
        case InvalidCase::ZeroReleaseSequence: release->releaseSequence = 0; break;
        case InvalidCase::ProtocolZero: release->updaterProtocol = 0; break;
        case InvalidCase::ProtocolTwo: release->updaterProtocol = 2; break;
        case InvalidCase::OsMajorZero: osVersion.major = 0; break;
        }

        QVERIFY(!makeInstalledRelease(release, installation, osVersion));
    }
};

QTEST_GUILESS_MAIN(InstalledReleaseTest)
#include "installedreleasetest.moc"
