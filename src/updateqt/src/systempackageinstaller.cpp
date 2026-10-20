#include "zzlogg/updateqt/systempackageinstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace zzlogg::updateqt {
namespace {
constexpr quint64 maximumPackageSize = 512ULL * 1024 * 1024;

struct StagedPackage {
    std::shared_ptr<QTemporaryDir> directory;
    QString path;
    QString error;
};

bool validArtifact(const update::Artifact& artifact)
{
    return artifact.os == "linux" && artifact.arch == "x64"
        && artifact.distribution == update::Distribution::Installer
        && (artifact.format == "deb" || artifact.format == "rpm")
        && artifact.size > 0 && artifact.size <= maximumPackageSize
        && artifact.sha256.size() == 64
        && std::all_of(artifact.sha256.begin(), artifact.sha256.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}

StagedPackage stagePackage(const QString& path, const update::Artifact& artifact)
{
    StagedPackage staged;
    const auto fail = [&](const QString& message) {
        staged.error = message;
        return staged;
    };
    // Reject symlinks in the supplied path; open the leaf without following it.
    // Even if a parent changes concurrently, only the bytes copied and hashed
    // below can reach the package manager, never a re-opened download pathname.
    QString ancestor = QFileInfo(path).absoluteFilePath();
    while (!ancestor.isEmpty()) {
        if (QFileInfo(ancestor).isSymLink())
            return fail(QObject::tr("The downloaded package path contains a symbolic link."));
        const auto parent = QFileInfo(ancestor).dir().absolutePath();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    QFile source;
#ifdef Q_OS_LINUX
    const int descriptor = ::open(QFile::encodeName(path).constData(),
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
        return fail(QObject::tr("Cannot open the downloaded package safely."));
    struct stat info {};
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)
        || info.st_size < 0 || quint64(info.st_size) != artifact.size) {
        ::close(descriptor);
        return fail(QObject::tr("The downloaded package is not a regular file of the expected size."));
    }
    if (!source.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return fail(QObject::tr("Cannot read the downloaded package."));
    }
#else
    Q_UNUSED(path);
    return fail(QObject::tr("System package installation is only supported on Linux."));
#endif
    staged.directory = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/zzlogg-install-XXXXXX");
    if (!staged.directory->isValid()
        || !QFile::setPermissions(staged.directory->path(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return fail(QObject::tr("Cannot create a private installation directory."));
    staged.path = staged.directory->filePath("ZzLogg-update." + QString::fromStdString(artifact.format));
    QFile destination(staged.path);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || !destination.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(QObject::tr("Cannot create the private installation package."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    quint64 copied = 0;
    while (!source.atEnd()) {
        const auto bytes = source.read(64 * 1024);
        if (source.error() != QFileDevice::NoError || bytes.isEmpty())
            return fail(QObject::tr("Reading the downloaded package failed."));
        if (quint64(bytes.size()) > artifact.size - copied)
            return fail(QObject::tr("The downloaded package size changed during verification."));
        if (destination.write(bytes) != bytes.size())
            return fail(QObject::tr("Writing the private installation package failed."));
        copied += quint64(bytes.size());
        hash.addData(bytes);
    }
    if (copied != artifact.size || hash.result().toHex().toStdString() != artifact.sha256)
        return fail(QObject::tr("The downloaded package failed size or SHA-256 verification. Download it again."));
    if (!destination.flush())
        return fail(QObject::tr("Saving the verified installation package failed."));
    destination.close();
    return staged;
}

SystemInstallCommand installedSystemCommand(const QString& format, const QString& package)
{
    QStringList executables;
    for (const auto* program : {"/usr/bin/pkexec", "/usr/bin/apt-get", "/usr/bin/rpm"}) {
        const QFileInfo file(QString::fromLatin1(program));
        if (file.isFile() && file.isExecutable()) executables.append(file.absoluteFilePath());
    }
#ifdef Q_OS_LINUX
    return systemInstallCommand(format, package, ::geteuid() == 0, executables);
#else
    Q_UNUSED(format);
    Q_UNUSED(package);
    return {};
#endif
}

void appendLog(const QString& path, const QString& message)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((message + '\n').toUtf8());
}
}

SystemInstallCommand systemInstallCommand(const QString& format, const QString& package,
    bool elevated, const QStringList& executables)
{
    SystemInstallCommand command;
    if (format == "deb" && executables.contains("/usr/bin/apt-get"))
        command = {"/usr/bin/apt-get", {"--reinstall", "--yes", "install", package}};
    else if (format == "rpm" && executables.contains("/usr/bin/rpm"))
        command = {"/usr/bin/rpm", {"--upgrade", "--replacepkgs", "--", package}};
    if (command.program.isEmpty() || elevated) return command;
    if (!executables.contains("/usr/bin/pkexec")) return {};
    command.arguments.prepend(command.program);
    command.program = "/usr/bin/pkexec";
    return command;
}

QString systemPackageFormat(const QByteArray& debOwner, const QByteArray& rpmOwner)
{
    if (debOwner.toLower() == "installed\nzzlogg\namd64\n") return "deb";
    if (rpmOwner.toLower() == "zzlogg\nx86_64\n") return "rpm";
    return {};
}

struct SystemPackageInstaller::Impl {
    CommandResolver resolver;
    QString log;
    bool active = false;
};

SystemPackageInstaller::SystemPackageInstaller(QObject* parent, CommandResolver resolver)
    : QObject(parent), impl_(std::make_unique<Impl>())
{
    impl_->resolver = resolver ? std::move(resolver) : installedSystemCommand;
}

// An active QProcess owns its own cleanup and staged file lifetime below. It is
// deliberately not our QObject child: destroying a UI must not kill a running
// system package transaction. The application prevents normal quit while busy.
SystemPackageInstaller::~SystemPackageInstaller() = default;
bool SystemPackageInstaller::busy() const { return impl_->active; }
QString SystemPackageInstaller::logPath() const { return impl_->log; }

void SystemPackageInstaller::complete(bool success, const QString& detail)
{
    if (!impl_->active) return;
    impl_->active = false;
    if (!impl_->log.isEmpty()) appendLog(impl_->log, detail);
    emit finished(success, detail);
}

void SystemPackageInstaller::start(const QString& packagePath, const update::Artifact& artifact)
{
    if (impl_->active) return;
    impl_->active = true;
    impl_->log.clear();
    if (!validArtifact(artifact)) {
        complete(false, tr("This is not a supported Linux x64 DEB or RPM installation package."));
        return;
    }
#ifndef Q_OS_LINUX
    complete(false, tr("System package installation is only supported on Linux."));
    return;
#endif
    auto* watcher = new QFutureWatcher<StagedPackage>(this);
    connect(watcher, &QFutureWatcher<StagedPackage>::finished, this, [this, watcher, artifact] {
        const auto staged = watcher->result();
        watcher->deleteLater();
        if (!staged.error.isEmpty()) { complete(false, staged.error); return; }
        const auto command = impl_->resolver(QString::fromStdString(artifact.format), staged.path);
        if (command.program.isEmpty()) {
            complete(false, tr("The required system package manager or pkexec authorization tool is unavailable."));
            return;
        }
        const auto logDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + "/updates/install-logs";
        if (!QDir().mkpath(logDirectory)) {
            complete(false, tr("Cannot create the installation log directory."));
            return;
        }
        impl_->log = logDirectory + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".log";
        QFile log(impl_->log);
        if (!log.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || !log.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            complete(false, tr("Cannot create the installation log."));
            return;
        }
        log.write((tr("Installing verified package using %1").arg(command.program) + '\n').toUtf8());
        log.close();
        auto* process = new QProcess;
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setStandardOutputFile(impl_->log, QIODevice::Append);
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                complete(false, tr("Cannot start the system package installer: %1").arg(process->errorString()));
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && exitCode == 0)
                    complete(true, tr("The system package manager installed the update successfully."));
                else if (status == QProcess::CrashExit)
                    complete(false, tr("The system package installer crashed (exit code %1). See the installation log.").arg(exitCode));
                else
                    complete(false, tr("The system package installer failed (exit code %1). See the installation log.").arg(exitCode));
            });
        // Context is the process, not the UI. Keep the private file until the
        // actual child exits even if this installer QObject has been destroyed.
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
            [process, staged](int, QProcess::ExitStatus) { Q_UNUSED(staged); process->deleteLater(); });
        connect(process, &QProcess::errorOccurred, process,
            [process, staged](QProcess::ProcessError error) {
                Q_UNUSED(staged);
                if (error == QProcess::FailedToStart) process->deleteLater();
            });
        process->start(command.program, command.arguments);
    });
    watcher->setFuture(QtConcurrent::run([packagePath, artifact] { return stagePackage(packagePath, artifact); }));
}
}
