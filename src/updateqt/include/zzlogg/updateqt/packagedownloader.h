#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include "packagecache.h"

namespace zzlogg::updateqt {
enum class DownloadError {
    InvalidPath, Busy, InvalidArtifact, InsufficientSpace, WriteFailed,
    SizeMismatch, HashMismatch, InvalidUrl, Http, Tls, Redirect, Timeout, Network, Cancelled
};
class PackageDownloader : public QObject {
    Q_OBJECT
public:
    explicit PackageDownloader(QObject* parent = nullptr);
    // Takes ownership of the independent transport. Durations are milliseconds.
    PackageDownloader(QNetworkAccessManager* transport, int totalMs, int inactivityMs,
                      QObject* parent = nullptr);
    ~PackageDownloader() override;
    void start(const update::Artifact&, const std::vector<std::string>& allowedHosts,
               const QString& cacheRoot);
    void cancel();
Q_SIGNALS:
    void progress(qint64 received, qint64 total);
    void succeeded(QString verifiedPath);
    void failed(zzlogg::updateqt::DownloadError error);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
Q_DECLARE_METATYPE(zzlogg::updateqt::DownloadError)
