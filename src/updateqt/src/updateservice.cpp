#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg/update/urlpolicy.h"
namespace zzlogg::updateqt {
namespace {
bool validChannel(Channel channel) { return channel==Channel::Stable || channel==Channel::Preview; }
CheckStatus stateStatus(StateError error) {
    switch(error) {
    case StateError::Busy: return CheckStatus::StateBusy;
    case StateError::WriteFailed: return CheckStatus::StateWriteFailed;
    case StateError::Replay: case StateError::Conflict: return CheckStatus::VerificationFailed;
    default: return CheckStatus::StateInvalid;
    }
}
}
UpdateService::UpdateService(FeedConfiguration config,std::shared_ptr<UpdateStateStore> store,
    std::optional<update::InstalledRelease> installed,Clock clock,QObject* parent,NetworkFactory factory)
    : QObject(parent),config_(std::move(config)),store_(std::move(store)),installed_(std::move(installed)),
      clock_(std::move(clock)),fetcher_(factory ? factory() : new QNetworkAccessManager,30000,10000)
{
    Q_ASSERT(store_ && clock_);
    connect(&fetcher_,&ManifestFetcher::succeeded,this,&UpdateService::received);
    connect(&fetcher_,&ManifestFetcher::failed,this,&UpdateService::failed);
    poll_.setInterval(60000);
    connect(&poll_,&QTimer::timeout,this,[this]{ requestCheck(channel_,CheckOrigin::Background); });
    poll_.start();
    QTimer::singleShot(10000,this,[this]{ requestCheck(channel_,CheckOrigin::Background); });
}
UpdateService::~UpdateService() { abandon(); }
bool UpdateService::configured(Channel channel) const
{
    const auto& url=channel==Channel::Stable ? config_.stableUrl : config_.previewUrl;
    if (!update::isAllowedUpdateUrl(url.toStdString(),config_.allowedHosts)) return false;
    for (const auto& key:config_.keys) {
        const bool purpose=config_.environment==update::TrustEnvironment::Production
            ? key.purpose==update::KeyPurpose::Production : key.purpose==update::KeyPurpose::Test;
        if (purpose && key.publicKey.size()==32 && !key.id.empty()) return true;
    }
    return false;
}
void UpdateService::publish(CheckStatus status,bool present)
{
    snapshot_={status,channel_,{},{},present};
    if(status!=CheckStatus::Idle && status!=CheckStatus::Checking && status!=CheckStatus::NotConfigured)
        snapshot_.checkedAt=clock_();
    emit snapshotChanged();
}
void UpdateService::abandon()
{
    active_=false; ++generation_;
    fetcher_.cancel();
}
bool UpdateService::changeChannel(Channel channel)
{
    if (channel==channel_) return true;
    const bool cancelled=active_;
    abandon();
    if (cancelled) {
        const auto saved=store_->recordCancellation(channel_,clock_());
        if (saved!=StateError::None) { publish(stateStatus(saved),true); return false; }
    }
    channel_=channel;
    snapshot_={CheckStatus::Idle,channel_,{},{},false};
    return true;
}
void UpdateService::requestCheck(Channel channel,CheckOrigin origin)
{
    if (!validChannel(channel) || (origin==CheckOrigin::Background && !automatic_)) return;
    if (active_ && channel==channel_) {
        if (origin==CheckOrigin::Manual && !manual_) {
            manual_=true; snapshot_.presentToUser=true; emit snapshotChanged();
        }
        return;
    }
    const bool changed=channel!=channel_;
    if (!changeChannel(channel)) return;
    abandon(); manual_=origin==CheckOrigin::Manual;
    if (!configured(channel)) { publish(CheckStatus::NotConfigured,manual_); return; }
    const auto state=store_->read(channel);
    if (!state.value) { publish(stateStatus(state.error),manual_); return; }
    if (!manual_ && !isCheckDue(*state.value,clock_())) {
        if (changed) emit snapshotChanged();
        return;
    }
    active_=true;
    const auto generation=generation_;
    const QPointer<UpdateService> guard(this);
    publish(CheckStatus::Checking,manual_);
    // A UI slot may cancel, switch channel, or close the owner during notification.
    if (!guard || generation_!=generation || !active_) return;
    const auto& url=channel==Channel::Stable ? config_.stableUrl : config_.previewUrl;
    fetcher_.start(QUrl(url,QUrl::StrictMode),config_.allowedHosts);
}
void UpdateService::received(const QByteArray& bytes)
{
    if (!active_) return;
    active_=false;
    const auto now=clock_();
    const auto state=store_->read(channel_);
    if (!state.value) { publish(stateStatus(state.error),manual_); return; }
    update::VerificationContext context{config_.keys,config_.environment,config_.allowedHosts,
        now,config_.buildTime,channel_==Channel::Stable ? "stable" : "preview",state.value->accepted};
    const auto verified=update::verifyManifest(bytes.toStdString(),context);
    if (!verified.value) {
        const auto saved=store_->recordFailure(channel_,now);
        publish(saved==StateError::None ? CheckStatus::VerificationFailed : stateStatus(saved),manual_);
        return;
    }
    const auto saved=store_->accept(channel_,verified.value->acceptedMetadata(),now);
    if (saved!=StateError::None) { publish(stateStatus(saved),manual_); return; }
    CheckSnapshot result{CheckStatus::ReleaseInformation,channel_,verified.value,{},false};
    result.checkedAt=now;
    if (installed_ && !installed_->developmentBuild && installed_->releaseSequence>0) {
        result.decision=update::selectUpdate(*verified.value,*installed_);
        switch(result.decision->status) {
        case update::DecisionStatus::Available: result.status=CheckStatus::Available; break;
        case update::DecisionStatus::NoUpdate: result.status=CheckStatus::UpToDate; break;
        default: result.status=CheckStatus::Unsupported; break;
        }
    }
    if(result.status==CheckStatus::ReleaseInformation && displayVersion_) {
        if(update::compareVersion(verified.value->manifest().version,*displayVersion_)>0)
            result.status=CheckStatus::ManualUpdateAvailable;
        else if(channel_==Channel::Stable)
            result.status=CheckStatus::UpToDate;
        // Preview snapshots can share a display version. Without a trusted
        // installed build identity we cannot claim this is the latest snapshot.
    }
    const bool skipped=state.value->skippedReleaseSequence
        && *state.value->skippedReleaseSequence==verified.value->manifest().releaseSequence;
    result.presentToUser=manual_ || (!skipped && (result.status==CheckStatus::Available
        || (result.status==CheckStatus::ReleaseInformation && !displayVersion_)
        || result.status==CheckStatus::ManualUpdateAvailable));
    snapshot_=std::move(result);
    emit snapshotChanged();
}
void UpdateService::failed(FetchError error)
{
    if (!active_) return;
    active_=false;
    const auto saved=error==FetchError::Cancelled ? store_->recordCancellation(channel_,clock_())
        : store_->recordFailure(channel_,clock_());
    publish(saved!=StateError::None ? stateStatus(saved)
        : error==FetchError::Cancelled ? CheckStatus::Cancelled : CheckStatus::NetworkError,manual_);
}
void UpdateService::cancel()
{
    if (!active_) return;
    abandon();
    const auto saved=store_->recordCancellation(channel_,clock_());
    publish(saved==StateError::None ? CheckStatus::Cancelled : stateStatus(saved),manual_);
}
void UpdateService::setDisplayVersion(std::optional<update::Version> version) { displayVersion_=version; }
void UpdateService::setAutomaticChecking(bool enabled) { automatic_=enabled; }
void UpdateService::setChannel(Channel channel)
{
    if (!validChannel(channel) || channel==channel_) return;
    if (!changeChannel(channel)) return;
    publish(CheckStatus::Idle,false);
}
void UpdateService::skipCurrentRelease()
{
    if (active_ || !snapshot_.release) return;
    const auto saved=store_->skip(snapshot_.channel,snapshot_.release->manifest().releaseSequence);
    if (saved!=StateError::None) { publish(stateStatus(saved),true); return; }
    snapshot_.presentToUser=false;
    emit snapshotChanged();
}
}
