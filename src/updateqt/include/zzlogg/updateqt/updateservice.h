#pragma once
#include "updatestate.h"
#include "manifestfetcher.h"
#include "zzlogg/update/policy.h"
#include <memory>

namespace zzlogg::updateqt {
enum class CheckOrigin { Manual, Background };
enum class CheckStatus {
    Idle, NotConfigured, Checking, Cancelled, UpToDate, Available,
    ReleaseInformation, Unsupported, NetworkError, VerificationFailed,
    StateInvalid, StateBusy, StateWriteFailed
};
struct FeedConfiguration {
    QString stableUrl, previewUrl;
    std::vector<update::TrustedKey> keys;
    std::vector<std::string> allowedHosts;
    qint64 buildTime=0;
    update::TrustEnvironment environment=update::TrustEnvironment::Production;
};
FeedConfiguration productionFeedConfiguration();
struct CheckSnapshot {
    CheckStatus status=CheckStatus::Idle;
    Channel channel=Channel::Stable;
    std::optional<update::VerifiedManifest> release;
    std::optional<update::Decision> decision;
    bool presentToUser=false;
    qint64 checkedAt=0;
};
class UpdateService : public QObject {
    Q_OBJECT
public:
    using Clock=std::function<qint64()>;
    using NetworkFactory=std::function<QNetworkAccessManager*()>;
    UpdateService(FeedConfiguration, std::shared_ptr<UpdateStateStore>,
        std::optional<update::InstalledRelease>, Clock, QObject* parent=nullptr,
        NetworkFactory networkFactory={});
    ~UpdateService() override;
    void requestCheck(Channel, CheckOrigin);
    void cancel();
    void setAutomaticChecking(bool);
    void setChannel(Channel);
    void skipCurrentRelease();
    const CheckSnapshot& snapshot() const { return snapshot_; }
Q_SIGNALS:
    void snapshotChanged();
private:
    bool configured(Channel) const;
    void received(const QByteArray&);
    void failed(FetchError);
    void publish(CheckStatus, bool present);
    void abandon();
    bool changeChannel(Channel);
    FeedConfiguration config_;
    std::shared_ptr<UpdateStateStore> store_;
    std::optional<update::InstalledRelease> installed_;
    Clock clock_;
    ManifestFetcher fetcher_;
    QTimer poll_;
    CheckSnapshot snapshot_;
    Channel channel_=Channel::Stable;
    bool automatic_=true, active_=false, manual_=false;
    quint64 generation_=0;
};
}
