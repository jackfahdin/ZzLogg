#pragma once

#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>
#include "zzlogg/update/manifest.h"

namespace zzlogg::updateqt {
struct SystemInstallCommand {
    QString program;
    QStringList arguments;
};

// Uses only explicitly located system tools, with arguments passed without a shell.
SystemInstallCommand systemInstallCommand(const QString& format, const QString& package,
    bool elevated, const QStringList& executables);
QString systemPackageFormat(const QByteArray& debOwner, const QByteArray& rpmOwner);

class SystemPackageInstaller : public QObject {
    Q_OBJECT
public:
    using CommandResolver = std::function<SystemInstallCommand(const QString&, const QString&)>;
    explicit SystemPackageInstaller(QObject* parent = nullptr, CommandResolver resolver = {});
    ~SystemPackageInstaller() override;
    bool busy() const;
    QString logPath() const;
    void start(const QString& packagePath, const update::Artifact& artifact);
Q_SIGNALS:
    void finished(bool success, const QString& detail);
private:
    void complete(bool success, const QString& detail);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
