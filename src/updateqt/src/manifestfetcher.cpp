#include "zzlogg/updateqt/manifestfetcher.h"
#include "zzlogg/update/urlpolicy.h"
namespace zzlogg::updateqt {
namespace {
constexpr qsizetype limit = 256 * 1024;
bool allowed(const QUrl& url, const std::vector<std::string>& hosts)
{
    return url.isValid() && update::isAllowedUpdateUrl(url.toEncoded(QUrl::FullyEncoded).toStdString(),hosts);
}
bool redirectStatus(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}
bool validLocation(const QByteArray& value)
{
    if (value.isEmpty() || value.size() > 8192) return false;
    // Validate before QUrl can normalize whitespace, backslashes or bad escapes.
    constexpr std::string_view chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?[]@!$&'()*+,;=%";
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c-'0'
        : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; };
    for (qsizetype i=0; i<value.size(); ++i) {
        if (chars.find(value[i]) == chars.npos) return false;
        if (value[i] == '%') {
            if (i+2 >= value.size() || hex(value[i+1]) < 0 || hex(value[i+2]) < 0) return false;
            const int byte=hex(value[i+1])*16+hex(value[i+2]);
            if (byte < 32 || byte == 127 || byte == '\\') return false;
            i+=2;
        }
    }
    return true;
}
}
ManifestFetcher::ManifestFetcher(QObject* parent) : ManifestFetcher(new QNetworkAccessManager,30000,10000,parent) {}
ManifestFetcher::ManifestFetcher(QNetworkAccessManager* transport,int totalMs,int inactivityMs,QObject* parent)
    : QObject(parent),transport_(transport),totalMs_(qMax(1,totalMs)),inactivityMs_(qMax(1,inactivityMs))
{
    Q_ASSERT(transport_);
    transport_->setParent(this);
    total_.setSingleShot(true);
    inactivity_.setSingleShot(true);
    connect(&total_,&QTimer::timeout,this,[this]{ fail(FetchError::Timeout); });
    connect(&inactivity_,&QTimer::timeout,this,[this]{ fail(FetchError::Timeout); });
}
ManifestFetcher::~ManifestFetcher()
{
    active_=false;
    releaseReply();
}
void ManifestFetcher::releaseReply()
{
    ++generation_;
    if (reply_) {
        auto* old=reply_.data();
        reply_.clear();
        disconnect(old,nullptr,this,nullptr);
        if (!old->isFinished()) old->abort();
        old->deleteLater();
    }
}
void ManifestFetcher::start(const QUrl& url,const std::vector<std::string>& hosts)
{
    // A new start supersedes the previous operation without a stale terminal signal.
    active_=false;
    total_.stop(); inactivity_.stop(); releaseReply();
    hosts_=hosts; bytes_.clear(); redirects_=0; active_=true;
    if (!allowed(url,hosts_)) { fail(FetchError::InvalidUrl); return; }
    total_.start(totalMs_);
    request(url);
}
void ManifestFetcher::request(const QUrl& url)
{
    if (!allowed(url,hosts_)) { fail(FetchError::InvalidUrl); return; }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Accept-Encoding","identity");
    request.setRawHeader("Accept","application/json");
    request.setTransferTimeout(inactivityMs_);
    reply_=transport_->get(request);
    reply_->setReadBufferSize(16*1024);
    const auto generation=generation_;
    auto current=[this,generation]{ return active_ && reply_ && generation_==generation; };
    connect(reply_,&QNetworkReply::metaDataChanged,this,[this,current]{ if(current()) headers(); });
    connect(reply_,&QNetworkReply::readyRead,this,[this,current]{ if(current()) consume(); });
    connect(reply_,&QNetworkReply::sslErrors,this,[this,current](const QList<QSslError>&){
        if(current()) fail(FetchError::Tls);
    });
    connect(reply_,&QNetworkReply::finished,this,[this,current]{ if(current()) finish(); });
    inactivity_.start(inactivityMs_);
}
bool ManifestFetcher::headers()
{
    if (!active_ || !reply_) return false;
    if (!allowed(reply_->url(),hosts_)) { fail(FetchError::InvalidUrl); return false; }
    const auto encoding=reply_->rawHeader("Content-Encoding").trimmed().toLower();
    if (!encoding.isEmpty() && encoding != "identity") { fail(FetchError::Http); return false; }
    if (reply_->hasRawHeader("Content-Length")) {
        const auto value=reply_->rawHeader("Content-Length");
        bool numeric=!value.isEmpty();
        for (const char ch:value) numeric=numeric && ch>='0' && ch<='9';
        bool ok=false;
        const auto length=value.toULongLong(&ok);
        if (!numeric || !ok) { fail(FetchError::Http); return false; }
        if (length > quint64(limit)) { fail(FetchError::TooLarge); return false; }
    }
    return true;
}
void ManifestFetcher::consume()
{
    if (!headers()) return;
    while (reply_->bytesAvailable()>0) {
        const auto chunk=reply_->read(qMin(qsizetype(8192),limit+1-bytes_.size()));
        if (chunk.isEmpty()) break;
        bytes_.append(chunk);
        if (bytes_.size()>limit) { fail(FetchError::TooLarge); return; }
        inactivity_.start(inactivityMs_);
    }
}
void ManifestFetcher::finish()
{
    const QPointer<ManifestFetcher> guard(this);
    const auto generation=generation_;
    consume();
    // A failure slot can synchronously destroy us or start a replacement request.
    if (!guard || generation_!=generation || !active_ || !reply_) return;
    if (reply_->error()!=QNetworkReply::NoError) {
        const auto error=reply_->error();
        fail(error==QNetworkReply::SslHandshakeFailedError ? FetchError::Tls
            : error==QNetworkReply::TimeoutError ? FetchError::Timeout : FetchError::Network);
        return;
    }
    const auto status=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (redirectStatus(status)) {
        const auto location=reply_->rawHeader("Location");
        if (redirects_>=3 || !validLocation(location)) { fail(FetchError::Redirect); return; }
        const auto target=reply_->url().resolved(QUrl::fromEncoded(location,QUrl::StrictMode));
        if (!allowed(target,hosts_)) { fail(FetchError::Redirect); return; }
        ++redirects_;
        releaseReply(); bytes_.clear(); request(target);
        return;
    }
    if (status!=200 || bytes_.isEmpty()) { fail(FetchError::Http); return; }
    if (reply_->hasRawHeader("Content-Length")
        && reply_->rawHeader("Content-Length").toULongLong()!=quint64(bytes_.size())) {
        fail(FetchError::Http); return;
    }
    active_=false; total_.stop(); inactivity_.stop(); releaseReply();
    const auto result=std::move(bytes_);
    emit succeeded(result);
}
void ManifestFetcher::fail(FetchError error)
{
    if (!active_) return;
    active_=false; total_.stop(); inactivity_.stop(); releaseReply(); bytes_.clear();
    emit failed(error);
}
void ManifestFetcher::cancel() { fail(FetchError::Cancelled); }
}
