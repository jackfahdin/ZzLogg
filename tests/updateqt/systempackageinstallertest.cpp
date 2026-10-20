#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QStandardPaths>
#include "zzlogg/updateqt/systempackageinstaller.h"

using namespace zzlogg::updateqt;
class SystemPackageInstallerTest : public QObject {
    Q_OBJECT
private:
    static zzlogg::update::Artifact validArtifact()
    {
        zzlogg::update::Artifact artifact;
        artifact.os="linux"; artifact.arch="x64"; artifact.format="deb";
        artifact.distribution=zzlogg::update::Distribution::Installer;
        artifact.size=7;
        artifact.sha256=QCryptographicHash::hash("package", QCryptographicHash::Sha256).toHex().toStdString();
        return artifact;
    }
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void routesPackagesThroughTheirManager()
    {
        const QString path = QStringLiteral("/tmp/更新包 with spaces.deb");
        const auto deb = systemInstallCommand("deb", path, false,
            {"/usr/bin/pkexec", "/usr/bin/apt-get", "/usr/bin/dnf"});
        QCOMPARE(deb.program, QStringLiteral("/usr/bin/pkexec"));
        QCOMPARE(deb.arguments, (QStringList{"/usr/bin/apt-get", "--reinstall", "--yes", "install", path}));
        const auto rpm = systemInstallCommand("rpm", "/tmp/new.rpm", true,
            {"/usr/bin/apt-get", "/usr/bin/rpm"});
        QCOMPARE(rpm.program, QStringLiteral("/usr/bin/rpm"));
        QCOMPARE(rpm.arguments, (QStringList{"--upgrade", "--replacepkgs", "--", "/tmp/new.rpm"}));
        QVERIFY(systemInstallCommand("deb", path, false, {"/usr/bin/apt-get"}).program.isEmpty());
        QVERIFY(systemInstallCommand("zip", path, true, {"/usr/bin/apt-get"}).program.isEmpty());
    }
    void requiresInstalledPackageOwnership()
    {
        QCOMPARE(systemPackageFormat("installed\nzzlogg\namd64\n", {}), QStringLiteral("deb"));
        QCOMPARE(systemPackageFormat({}, "ZzLogg\nx86_64\n"), QStringLiteral("rpm"));
        QVERIFY(systemPackageFormat("config-files\nzzlogg\namd64\n", {}).isEmpty());
        QVERIFY(systemPackageFormat("installed\nother\namd64\n", {}).isEmpty());
        QVERIFY(systemPackageFormat({}, "ZzLogg\naarch64\n").isEmpty());
    }
    void rejectsTamperedPackageWithoutLaunching()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("update.deb");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("tampered"); file.close();
        bool launched = false;
        SystemPackageInstaller installer(nullptr, [&](const QString&, const QString&) {
            launched = true;
            return SystemInstallCommand{};
        });
        QSignalSpy result(&installer, &SystemPackageInstaller::finished);
        zzlogg::update::Artifact artifact;
        artifact.os="linux"; artifact.arch="x64"; artifact.format="deb";
        artifact.distribution=zzlogg::update::Distribution::Installer;
        artifact.size=8; artifact.sha256=std::string(64, '0');
        installer.start(path, artifact);
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(!result.first().at(0).toBool());
        QVERIFY(!launched);
        QVERIFY(!installer.busy());
        QVERIFY(QFile::exists(path));
    }
#ifdef Q_OS_LINUX
    void installsVerifiedPrivateCopyAndReportsSuccess()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("更新包 with spaces.deb");
        QFile original(path); QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("package"); original.close();
        QString stagedPath;
        int commands = 0;
        bool privateCopy = false;
        SystemPackageInstaller installer(nullptr, [&](const QString& format, const QString& staged) {
            ++commands;
            stagedPath = staged;
            QFile stagedFile(staged);
            privateCopy = format == "deb" && staged != path && stagedFile.open(QIODevice::ReadOnly)
                && stagedFile.readAll() == "package"
                && (QFileInfo(staged).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther
                     | QFileDevice::WriteGroup | QFileDevice::WriteOther)) == 0;
            // Replacing the download after verification cannot change installed bytes.
            QFile changed(path);
            if (!changed.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                privateCopy = false;
                return SystemInstallCommand{};
            }
            changed.write("modified"); changed.close();
            return SystemInstallCommand{QStringLiteral("/bin/sh"),
                {"-c", "cat \"$1\"; echo installed-ok", "fixture", staged}};
        });
        QSignalSpy result(&installer, &SystemPackageInstaller::finished);
        installer.start(path, validArtifact());
        QVERIFY(installer.busy());
        installer.start(path, validArtifact());
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(result.first().at(0).toBool());
        QCOMPARE(commands, 1);
        QVERIFY(privateCopy);
        QVERIFY(!installer.busy());
        QTRY_VERIFY(!QFile::exists(stagedPath));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("modified"));
        QFile log(installer.logPath()); QVERIFY(log.open(QIODevice::ReadOnly));
        const auto output = log.readAll();
        QVERIFY(output.contains("packageinstalled-ok"));
        QVERIFY(!output.contains("modified"));
    }
    void rejectsSymlinkSourceAndInvalidMetadata()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("update.deb");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("package"); file.close();
        const auto link = temp.filePath("linked.deb");
        QVERIFY(QFile::link(path, link));
        int commands = 0;
        SystemPackageInstaller installer(nullptr, [&](const QString&, const QString&) {
            ++commands; return SystemInstallCommand{};
        });
        QSignalSpy result(&installer, &SystemPackageInstaller::finished);
        installer.start(link, validArtifact());
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(!result.last().at(0).toBool());
        auto artifact = validArtifact(); artifact.os = "windows";
        installer.start(path, artifact);
        QTRY_COMPARE(result.count(), 2);
        QVERIFY(!result.last().at(0).toBool());
        artifact = validArtifact(); artifact.size = 513ULL * 1024 * 1024;
        installer.start(path, artifact);
        QTRY_COMPARE(result.count(), 3);
        QVERIFY(!result.last().at(0).toBool());
        QCOMPARE(commands, 0);
    }
    void destroyingInstallerDoesNotKillActiveTransaction()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("update.deb");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("package"); file.close();
        const auto ready = temp.filePath("ready");
        const auto proceed = temp.filePath("proceed");
        const auto installed = temp.filePath("installed");
        QString stagedPath;
        auto installer = std::make_unique<SystemPackageInstaller>(nullptr,
            [&](const QString&, const QString& staged) {
                stagedPath = staged;
                return SystemInstallCommand{QStringLiteral("/bin/sh"),
                    {"-c", "touch \"$2\"; while [ ! -e \"$3\" ]; do sleep .05; done; cat \"$1\" > \"$4\"",
                     "fixture", staged, ready, proceed, installed}};
            });
        installer->start(path, validArtifact());
        QTRY_VERIFY(QFile::exists(ready));
        installer.reset();
        QVERIFY(QFile::exists(stagedPath));
        QFile release(proceed); QVERIFY(release.open(QIODevice::WriteOnly)); release.close();
        QTRY_VERIFY(QFile::exists(installed));
        QFile result(installed); QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("package"));
        QTRY_VERIFY(!QFile::exists(stagedPath));
    }
    void reportsFailedProcessStart()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("update.deb");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("package"); file.close();
        SystemPackageInstaller installer(nullptr, [](const QString&, const QString&) {
            return SystemInstallCommand{QStringLiteral("/nonexistent/zzlogg-package-manager"), {}};
        });
        QSignalSpy result(&installer, &SystemPackageInstaller::finished);
        installer.start(path, validArtifact());
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(!result.first().at(0).toBool());
        QVERIFY(!result.first().at(1).toString().isEmpty());
        QVERIFY(!installer.busy());
    }
    void reportsActualPackageProcessFailureAndRetainsDownload()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("update.deb");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("package"); file.close();
        SystemPackageInstaller installer(nullptr, [](const QString&, const QString&) {
            return SystemInstallCommand{QStringLiteral("/bin/sh"), {"-c", "echo manager-failed >&2; exit 23"}};
        });
        QSignalSpy result(&installer, &SystemPackageInstaller::finished);
        zzlogg::update::Artifact artifact;
        artifact.os="linux"; artifact.arch="x64"; artifact.format="deb";
        artifact.distribution=zzlogg::update::Distribution::Installer;
        artifact.size=7;
        artifact.sha256=QCryptographicHash::hash("package", QCryptographicHash::Sha256).toHex().toStdString();
        installer.start(path, artifact);
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(!result.first().at(0).toBool());
        QVERIFY(result.first().at(1).toString().contains("23"));
        QFile log(installer.logPath()); QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(log.readAll().contains("manager-failed"));
        QVERIFY(QFile::exists(path));
    }
#endif
};
QTEST_GUILESS_MAIN(SystemPackageInstallerTest)
#include "systempackageinstallertest.moc"
