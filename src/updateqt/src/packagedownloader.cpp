#include "zzlogg/updateqt/packagedownloader.h"
#include "networkpolicy_p.h"
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

namespace zzlogg::updateqt {
namespace {
DownloadError cacheError(CacheError error)
{
    switch(error) {
    case CacheError::InvalidPath: return DownloadError::InvalidPath;
    case CacheError::Busy: return DownloadError::Busy;
    case CacheError::InvalidArtifact: return DownloadError::InvalidArtifact;
    case CacheError::InsufficientSpace: return DownloadError::InsufficientSpace;
    case CacheError::WriteFailed: return DownloadError::WriteFailed;
    case CacheError::SizeMismatch: return DownloadError::SizeMismatch;
    case CacheError::HashMismatch: return DownloadError::HashMismatch;
    case CacheError::Cancelled: return DownloadError::Cancelled;
    case CacheError::None: break;
    }
    Q_UNREACHABLE_RETURN(DownloadError::WriteFailed);
}
DownloadError networkError(QNetworkReply::NetworkError error)
{
    return error==QNetworkReply::SslHandshakeFailedError ? DownloadError::Tls
        : error==QNetworkReply::TimeoutError ? DownloadError::Timeout : DownloadError::Network;
}
}
class PackageDownloader::Impl {
public:
    Impl(PackageDownloader* owner,QNetworkAccessManager* network,int totalMs,int inactivityMs)
        : owner(owner),network(network),totalMs(qMax(1,totalMs)),inactivityMs(qMax(1,inactivityMs))
    {
        Q_ASSERT(network);
        network->setParent(owner);
        total.setSingleShot(true); inactivity.setSingleShot(true);
        QObject::connect(&total,&QTimer::timeout,owner,[this]{fail(DownloadError::Timeout);});
        QObject::connect(&inactivity,&QTimer::timeout,owner,[this]{fail(DownloadError::Timeout);});
    }
    ~Impl() { stop(); }

    void releaseReply()
    {
        ++generation; queued=false; consuming=false;
        if(reply) {
            auto* old=reply.data(); reply.clear();
            QObject::disconnect(old,nullptr,owner,nullptr);
            if(!old->isFinished()) old->abort();
            old->deleteLater();
        }
    }
    void stop()
    {
        active=false; total.stop(); inactivity.stop(); releaseReply(); cache.reset();
    }
    void fail(DownloadError error)
    {
        if(!active) return;
        stop();
        Q_EMIT owner->failed(error);
    }
    void start(const update::Artifact& artifact,const std::vector<std::string>& allowedHosts,const QString& root)
    {
        stop(); hosts=allowedHosts; expected=artifact.size; received=0; redirects=0; active=true;
        // Artifact stores raw signed bytes: reject them before constructing a QUrl.
        if(!update::isAllowedUpdateUrl(artifact.url,hosts)) { fail(DownloadError::InvalidUrl); return; }
        cache=std::make_unique<PackageCache>(root,artifact);
        const auto error=cache->begin();
        if(error!=CacheError::None) { fail(cacheError(error)); return; }
        total.start(totalMs);
        request(QUrl::fromEncoded(QByteArray::fromStdString(artifact.url),QUrl::StrictMode));
    }
    void request(const QUrl& url)
    {
        if(!detail::allowed(url,hosts)) { fail(DownloadError::InvalidUrl); return; }
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);
        request.setRawHeader("Accept-Encoding","identity");
        request.setTransferTimeout(inactivityMs);
        reply=network->get(request);
        reply->setReadBufferSize(128*1024);
        const auto token=generation;
        auto current=[this,token]{return active && reply && token==generation;};
        QObject::connect(reply,&QNetworkReply::metaDataChanged,owner,[this,current]{if(current()) headers();});
        QObject::connect(reply,&QNetworkReply::readyRead,owner,[this,current]{if(current()) consume();});
        QObject::connect(reply,&QNetworkReply::finished,owner,[this,current]{if(current()) consume();});
        QObject::connect(reply,&QNetworkReply::sslErrors,owner,[this,current](const QList<QSslError>&){
            if(current()) fail(DownloadError::Tls);
        });
        inactivity.start(inactivityMs);
    }
    bool headers()
    {
        if(!active || !reply) return false;
        if(!detail::allowed(reply->url(),hosts)) { fail(DownloadError::InvalidUrl); return false; }
        const auto statusValue=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if(!statusValue.isValid()) {
            if(reply->isFinished()) fail(reply->error()!=QNetworkReply::NoError ? networkError(reply->error()) : DownloadError::Http);
            return false;
        }
        const auto status=statusValue.toInt();
        if(detail::redirectStatus(status)) {
            const auto location=reply->rawHeader("Location");
            if(redirects>=3 || !detail::validLocation(location)) { fail(DownloadError::Redirect); return false; }
            const auto target=reply->url().resolved(QUrl::fromEncoded(location,QUrl::StrictMode));
            if(!detail::allowed(target,hosts)) { fail(DownloadError::Redirect); return false; }
            // No redirect response bytes are consumed or written. Aborting bounds even endless bodies.
            ++redirects; releaseReply(); request(target);
            return false;
        }
        if(status!=200) { fail(DownloadError::Http); return false; }
        const auto encoding=reply->rawHeader("Content-Encoding").trimmed().toLower();
        if(!encoding.isEmpty() && encoding!="identity") { fail(DownloadError::Http); return false; }
        if(reply->hasRawHeader("Content-Length")) {
            const auto value=reply->rawHeader("Content-Length");
            bool numeric=!value.isEmpty();
            for(char ch:value) numeric=numeric && ch>='0' && ch<='9';
            bool ok=false; const auto length=value.toULongLong(&ok);
            if(!numeric || !ok) { fail(DownloadError::Http); return false; }
            if(length!=expected) { fail(DownloadError::SizeMismatch); return false; }
        }
        return true;
    }
    void consume()
    {
        if(queued || consuming) return;
        if(!headers()) return;
        const QPointer<PackageDownloader> guard(owner);
        const auto token=generation;
        consuming=true;
        qint64 budget=1024*1024;
        while(reply->bytesAvailable()>0 && budget>0) {
            const auto chunk=reply->read(qMin(qint64(64*1024),budget));
            if(chunk.isEmpty()) break;
            const auto error=cache->append(chunk);
            if(error!=CacheError::None) { fail(cacheError(error)); return; }
            received+=chunk.size(); budget-=chunk.size(); inactivity.start(inactivityMs);
            Q_EMIT owner->progress(received,qint64(expected));
            // A signal may delete owner (and this Impl), replace the operation, or cancel it.
            if(!guard || token!=generation || !active || !reply) return;
        }
        consuming=false;
        if(reply->bytesAvailable()>0 && budget==0) {
            queued=true;
            QTimer::singleShot(0,owner,[this,token]{
                if(!active || token!=generation) return;
                queued=false; consume();
            });
            return;
        }
        if(!reply->isFinished()) return;
        if(reply->error()!=QNetworkReply::NoError) { fail(networkError(reply->error())); return; }
        const auto error=cache->finish();
        if(error!=CacheError::None) { fail(cacheError(error)); return; }
        const auto path=cache->verifiedPath();
        stop();
        Q_EMIT owner->succeeded(path);
    }

    PackageDownloader* owner;
    QNetworkAccessManager* network;
    QPointer<QNetworkReply> reply;
    QTimer total,inactivity;
    int totalMs,inactivityMs;
    quint64 generation=0,expected=0;
    qint64 received=0;
    unsigned redirects=0;
    bool active=false,queued=false,consuming=false;
    std::vector<std::string> hosts;
    std::unique_ptr<PackageCache> cache;
};
PackageDownloader::PackageDownloader(QObject* parent)
    : PackageDownloader(new QNetworkAccessManager,30*60*1000,30000,parent) {}
PackageDownloader::PackageDownloader(QNetworkAccessManager* transport,int totalMs,int inactivityMs,QObject* parent)
    : QObject(parent),impl_(std::make_unique<Impl>(this,transport,totalMs,inactivityMs)) {}
PackageDownloader::~PackageDownloader() = default;
void PackageDownloader::start(const update::Artifact& artifact,const std::vector<std::string>& hosts,const QString& root)
{ impl_->start(artifact,hosts,root); }
void PackageDownloader::cancel() { impl_->fail(DownloadError::Cancelled); }
}
