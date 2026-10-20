#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <memory>
// The application must save session/settings and reserve its installation activity
// lease before start(). Only readyToExit permits graceful application shutdown.
class PortableUpdateInstaller final:public QObject {
    Q_OBJECT
public:
    explicit PortableUpdateInstaller(QObject* parent=nullptr);
    ~PortableUpdateInstaller() override;
    bool busy() const;
    QString logPath() const;
    void start(const QString& packagePath,const QByteArray& sha256Hex,qint64 expectedSize,
               const QString& installRoot);
    void cancel();
Q_SIGNALS:
    void readyToExit();
    void failed(const QString& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
