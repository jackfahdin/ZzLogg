#pragma once
#include "updateservice.h"
#include "packagedownloader.h"
#include "zzlogg/update/executionselection.h"

namespace zzlogg::updateqt {
enum class DownloadStatus { Idle, Downloading, Verified, Cancelled, Failed, Unavailable };
struct DownloadSnapshot {
    DownloadStatus status=DownloadStatus::Idle;
    qint64 received=0, total=0;
    std::optional<DownloadError> error;
    QString verifiedPath;
    std::optional<update::UpdateSelection> selection;
};
class UpdateDownloadService : public QObject {
    Q_OBJECT
public:
    using Clock=UpdateService::Clock;
    using NetworkFactory=UpdateService::NetworkFactory;
    UpdateDownloadService(FeedConfiguration, std::optional<update::InstalledRelease>,
        QString cacheRoot, Clock, QObject* parent=nullptr, NetworkFactory networkFactory={});
    ~UpdateDownloadService() override;
    void requestDownload(const CheckSnapshot&);
    void cancel();
    void invalidate();
    const DownloadSnapshot& snapshot() const { return snapshot_; }
Q_SIGNALS:
    void snapshotChanged();
private:
    bool configured(Channel) const;
    bool trustedNow(const update::VerifiedManifest&,Channel) const;
    void watchDownload(quint64 generation);
    void abandon();
    void publish(DownloadStatus,std::optional<DownloadError> error={});
    FeedConfiguration config_;
    std::optional<update::InstalledRelease> installed_;
    QString cacheRoot_;
    Clock clock_;
    PackageDownloader downloader_;
    std::optional<update::VerifiedManifest> release_;
    std::optional<update::Artifact> artifact_;
    Channel channel_=Channel::Stable;
    bool active_=false;
    quint64 generation_=0;
    DownloadSnapshot snapshot_;
};
}
