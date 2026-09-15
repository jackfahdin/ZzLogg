#pragma once
#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <vector>
#include <string>

namespace zzlogg::updateqt {
enum class FetchError { InvalidUrl, Tls, Http, Network, TooLarge, Timeout, Redirect, Cancelled };
class ManifestFetcher : public QObject {
    Q_OBJECT
public:
    explicit ManifestFetcher(QObject* parent = nullptr);
    // Takes ownership of the transport. Short timeouts are for isolated tests only.
    ManifestFetcher(QNetworkAccessManager* transport, int totalMs, int inactivityMs, QObject* parent = nullptr);
    ~ManifestFetcher() override;
    void start(const QUrl&, const std::vector<std::string>& allowedHosts);
    void cancel();
Q_SIGNALS:
    void succeeded(QByteArray bytes);
    void failed(zzlogg::updateqt::FetchError error);
private:
    void request(const QUrl&);
    bool headers();
    void consume();
    void finish();
    void fail(FetchError);
    void releaseReply();
    QNetworkAccessManager* transport_;
    QPointer<QNetworkReply> reply_;
    QTimer total_, inactivity_;
    int totalMs_, inactivityMs_;
    quint64 generation_ = 0;
    unsigned redirects_ = 0;
    bool active_ = false;
    QByteArray bytes_;
    std::vector<std::string> hosts_;
};
}
Q_DECLARE_METATYPE(zzlogg::updateqt::FetchError)
