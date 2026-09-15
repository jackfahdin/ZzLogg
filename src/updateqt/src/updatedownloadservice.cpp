#include "zzlogg/updateqt/updatedownloadservice.h"
#include "zzlogg/updateqt/updatecachepaths.h"
#include "zzlogg/update/urlpolicy.h"
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <algorithm>
#include <set>
namespace zzlogg::updateqt {
UpdateDownloadService::UpdateDownloadService(FeedConfiguration config,std::optional<update::InstalledRelease> installed,
    QString cacheRoot,Clock clock,QObject* parent,NetworkFactory factory)
    :QObject(parent),config_(std::move(config)),installed_(std::move(installed)),
     cacheRoot_(std::move(cacheRoot)),clock_(std::move(clock)),
     downloader_(factory ? factory() : new QNetworkAccessManager,30*60*1000,30000)
{}
void UpdateDownloadService::watchDownload(quint64 generation)
{
    connect(&downloader_,&PackageDownloader::progress,this,[this,generation](qint64 received,qint64 total){
        if(!active_ || generation_!=generation) return;
        snapshot_.received=received; snapshot_.total=total;
        Q_EMIT snapshotChanged();
    });
    connect(&downloader_,&PackageDownloader::failed,this,[this,generation](DownloadError error){
        if(!active_ || generation_!=generation) return;
        abandon();
        publish(error==DownloadError::Cancelled ? DownloadStatus::Cancelled : DownloadStatus::Failed,error);
    });
    connect(&downloader_,&PackageDownloader::succeeded,this,[this,generation](const QString& path){
        if(!active_ || generation_!=generation || !release_) return;
        // No terminal result from an expired or invalidated operation can grant a usable path.
        if(!trustedNow(*release_,channel_)) {
            abandon(); publish(DownloadStatus::Unavailable); return;
        }
        active_=false; ++generation_;
        snapshot_.status=DownloadStatus::Verified; snapshot_.error.reset(); snapshot_.verifiedPath=path;
        // This path certifies this download only. Installation must verify the signature and bytes again.
        Q_EMIT snapshotChanged();
    });
}
UpdateDownloadService::~UpdateDownloadService() { abandon(); }
bool UpdateDownloadService::configured(Channel channel) const
{
    if(channel!=Channel::Stable && channel!=Channel::Preview) return false;
    if(!clock_) return false;
    const bool test=config_.environment==update::TrustEnvironment::Test;
    if(config_.environment!=update::TrustEnvironment::Production && !test) return false;
    if(test!=QStandardPaths::isTestModeEnabled()) return false;
    if(!test) {
        const auto systemRoot=updateCachePath();
        if(systemRoot.isEmpty() || cacheRoot_.isEmpty()
            || QDir::cleanPath(cacheRoot_)!=QDir::cleanPath(systemRoot)) return false;
    }
    const auto& url=channel==Channel::Stable ? config_.stableUrl : config_.previewUrl;
    if(!update::isAllowedUpdateUrl(url.toStdString(),config_.allowedHosts) || config_.keys.empty()) return false;
    std::set<std::string> ids;
    for(const auto& key:config_.keys) {
        if(key.id.empty() || key.publicKey.size()!=32 || !ids.insert(key.id).second
            || key.purpose!=(test ? update::KeyPurpose::Test : update::KeyPurpose::Production)) return false;
    }
    return true;
}
bool UpdateDownloadService::trustedNow(const update::VerifiedManifest& verified,Channel channel) const
{
    if(!configured(channel) || !installed_ || installed_->developmentBuild || installed_->releaseSequence==0)
        return false;
    if(verified.trustEnvironment()!=config_.environment) return false;
    const auto& signedKey=verified.signingKey();
    const bool sameKey=std::any_of(config_.keys.begin(),config_.keys.end(),[&](const auto& key){
        return key.id==signedKey.id && key.publicKey==signedKey.publicKey && key.purpose==signedKey.purpose;
    });
    if(!sameKey) return false;
    const auto& manifest=verified.manifest();
    const auto now=clock_();
    if(now<manifest.issuedAt || now>=manifest.expiresAt || now<0 || config_.buildTime<0
        || (config_.buildTime>now && config_.buildTime-now>86400)) return false;
    if(manifest.channel!=(channel==Channel::Stable ? "stable" : "preview")) return false;
    const auto decision=update::selectUpdate(verified,*installed_);
    return decision.status==update::DecisionStatus::Available && decision.artifact
        && update::isAllowedUpdateUrl(decision.artifact->url,config_.allowedHosts);
}
void UpdateDownloadService::abandon()
{
    active_=false; ++generation_; release_.reset(); artifact_.reset();
    disconnect(&downloader_,nullptr,this,nullptr);
    downloader_.cancel();
}
void UpdateDownloadService::publish(DownloadStatus status,std::optional<DownloadError> error)
{
    snapshot_={status,0,0,error,{}};
    Q_EMIT snapshotChanged();
}
void UpdateDownloadService::requestDownload(const CheckSnapshot& check)
{
    if(check.status!=CheckStatus::Available || !check.release || !trustedNow(*check.release,check.channel)) {
        abandon(); publish(DownloadStatus::Unavailable); return;
    }
    if(release_) {
        const bool same=channel_==check.channel
            && release_->acceptedMetadata().payloadDigest==check.release->acceptedMetadata().payloadDigest;
        if(same && (active_ || snapshot_.status==DownloadStatus::Verified)) return;
        // Switching metadata requires a new explicit confirmation after the old flow is invalidated.
        abandon(); publish(DownloadStatus::Unavailable); return;
    }
    const auto decision=update::selectUpdate(*check.release,*installed_);
    release_=check.release; artifact_=decision.artifact; channel_=check.channel;
    active_=true; const auto generation=++generation_;
    watchDownload(generation);
    snapshot_={DownloadStatus::Downloading,0,qint64(artifact_->size),{}, {}};
    const QPointer<UpdateDownloadService> guard(this);
    Q_EMIT snapshotChanged();
    if(!guard || generation_!=generation || !active_) return;
    // Notifications may advance time or disable the test environment before the first byte/request.
    if(!trustedNow(*release_,channel_)) { abandon(); publish(DownloadStatus::Unavailable); return; }
    // Keep arguments alive independently of synchronous progress/failure/cancellation callbacks.
    const auto artifact=*artifact_; const auto hosts=config_.allowedHosts; const auto root=cacheRoot_;
    downloader_.start(artifact,hosts,root);
}
void UpdateDownloadService::cancel()
{
    if(!active_) return;
    abandon(); publish(DownloadStatus::Cancelled,DownloadError::Cancelled);
}
void UpdateDownloadService::invalidate()
{
    abandon(); publish(DownloadStatus::Idle);
}
}
