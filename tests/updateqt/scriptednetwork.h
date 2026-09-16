#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QSslError>
#include <QPointer>
#include <deque>
#include <cstring>
#include <memory>

struct ReadMetrics { qint64 maximumRequest=0, totalRead=0, bufferLimit=0; };

struct NetworkScript {
    int status = 200;
    QByteArray body = "{}";
    QByteArray location, encoding, length;
    QUrl finalUrl;
    int responseDelayMs = 0;
    int chunkSize = 4096, intervalMs = 1;
    bool tlsError = false, hang = false, finishedOnly = false;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    std::shared_ptr<ReadMetrics> metrics=std::make_shared<ReadMetrics>();
};
class ScriptedReply : public QNetworkReply {
public:
    ScriptedReply(const QNetworkRequest& request, NetworkScript script, QObject* parent)
        : QNetworkReply(parent), script_(std::move(script)) {
        setRequest(request); setUrl(script_.finalUrl.isEmpty() ? request.url() : script_.finalUrl);
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,script_.status);
        if(!script_.location.isEmpty()) setRawHeader("Location",script_.location);
        if(!script_.encoding.isEmpty()) setRawHeader("Content-Encoding",script_.encoding);
        if(!script_.length.isEmpty()) setRawHeader("Content-Length",script_.length);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(script_.responseDelayMs,this,[this]{
            const QPointer<ScriptedReply> guard(this);
            if(!script_.finishedOnly) Q_EMIT metaDataChanged();
            if(!guard || aborted_) return;
            if(script_.tlsError) { Q_EMIT sslErrors({QSslError(QSslError::SelfSignedCertificate)}); return; }
            if(!script_.hang) tick();
        });
    }
    void abort() override {
        aborted_=true;
        setError(OperationCanceledError,"cancelled");
        // Deliberately deliver a late finish, like a queued transport callback.
        QTimer::singleShot(0,this,[this]{setFinished(true); Q_EMIT finished();});
    }
    qint64 bytesAvailable() const override { return buffer_.size()+QNetworkReply::bytesAvailable(); }
    void setReadBufferSize(qint64 size) override {
        script_.metrics->bufferLimit=size; QNetworkReply::setReadBufferSize(size);
    }
protected:
    qint64 readData(char* data,qint64 maximum) override {
        script_.metrics->maximumRequest=qMax(script_.metrics->maximumRequest,maximum);
        const auto size=qMin(maximum,qint64(buffer_.size()));
        script_.metrics->totalRead+=size;
        if(!size) return isFinished() ? -1 : 0;
        std::memcpy(data,buffer_.constData(),size); buffer_.remove(0,size); return size;
    }
private:
    void tick() {
        const QPointer<ScriptedReply> guard(this);
        if(!guard || aborted_) return;
        const auto count=qMin(qsizetype(script_.chunkSize),script_.body.size()-offset_);
        if(count>0) {
            buffer_.append(script_.body.constData()+offset_,count); offset_+=count;
            if(!script_.finishedOnly) Q_EMIT readyRead();
        }
        if(!guard || aborted_) return;
        if(offset_<script_.body.size()) {
            QTimer::singleShot(script_.intervalMs,this,[this]{tick();});
        } else {
            if(script_.error!=NoError) setError(script_.error,"scripted network error");
            setFinished(true); Q_EMIT finished();
        }
    }
    NetworkScript script_;
    QByteArray buffer_;
    qsizetype offset_=0;
    bool aborted_=false;
};
class ScriptedNetworkManager : public QNetworkAccessManager {
public:
    std::deque<NetworkScript> scripts;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply* createRequest(Operation,const QNetworkRequest& request,QIODevice*) override {
        requests.append(request);
        NetworkScript script;
        if(scripts.empty()) script.error=QNetworkReply::UnknownNetworkError;
        else { script=std::move(scripts.front()); scripts.pop_front(); }
        return new ScriptedReply(request,std::move(script),this);
    }
};
