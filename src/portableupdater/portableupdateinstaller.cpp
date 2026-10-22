#include "portableupdateinstaller.h"
#include "package.h"
#include "storagecontext.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
#include <atomic>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
struct Outcome { zzlogg::portable::PreparedPackage package; QString error; };
bool save(const QString& path,const QByteArray& data) {
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit();
}

}
struct PortableUpdateInstaller::Impl {
    QFutureWatcher<Outcome> watcher;
    QTimer timer;
    QElapsedTimer handshake;
    std::shared_ptr<std::atomic_bool> cancelled;
    QString stage,root,dataDirectory;
    bool busy=false,handedOff=false;
#ifdef Q_OS_WIN
    HANDLE child=nullptr;
    ~Impl() {if(child) CloseHandle(child);}
#endif
};
PortableUpdateInstaller::PortableUpdateInstaller(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()) {
    impl_->timer.setInterval(100);
    connect(&impl_->watcher,&QFutureWatcher<Outcome>::finished,this,[this] {
        const auto result=impl_->watcher.result();
        auto fail=[this](const QString& error) {
            impl_->busy=false; save(logPath(),error.toUtf8()+"\n");Q_EMIT failed(error+tr("\nUpdate log: %1").arg(logPath()));
        };
        if(!result.error.isEmpty()) {fail(result.error);return;}
        if(impl_->cancelled->load()) {fail(tr("Portable update cancelled."));return;}
#ifdef Q_OS_WIN
        QByteArray plan="ZZLOGG_PORTABLE_UPDATE_1\n";
        plan+=QByteArray::number(result.package.oldFiles.size())+'\n';
        for(const auto& path:result.package.oldFiles) plan+=QByteArray::fromStdString(path)+'\n';
        plan+=QByteArray::number(result.package.files.size())+'\n';
        for(const auto& file:result.package.files) plan+=QByteArray::fromStdString(file.sha256)+' '+
            QByteArray::number(file.size)+' '+QByteArray::fromStdString(file.path)+'\n';
        if(!save(impl_->stage+"/plan.txt",plan)) {fail(tr("Cannot save the portable update plan."));return;}
        const auto helper=impl_->stage+"/ZzLoggPortableUpdate.exe";
        if(!QFile::copy(impl_->root+"/ZzLoggPortableUpdate.exe",helper)) {fail(tr("Cannot copy the portable update helper. Extract a complete portable release first."));return;}
        qint64 childPid=0;
        if(!QProcess::startDetached(helper,{QDir::toNativeSeparators(impl_->root),QDir::toNativeSeparators(impl_->stage),
            QString::number(QCoreApplication::applicationPid()),QString::fromLatin1(QCryptographicHash::hash(plan,QCryptographicHash::Sha256).toHex()),
            QString::fromLatin1(result.package.oldManifestSha256),QDir::toNativeSeparators(impl_->dataDirectory)},impl_->stage,&childPid)) {
            fail(tr("The portable update helper could not start."));return;
        }
        impl_->child=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,static_cast<DWORD>(childPid));
        if(!impl_->child) {save(impl_->stage+"/cancel","cancel\n");fail(tr("Cannot monitor the portable update helper."));return;}
        impl_->handshake.start();impl_->timer.start();
#else
        fail(tr("Portable ZIP installation is only supported on Windows."));
#endif
    });
#ifdef Q_OS_WIN
    connect(&impl_->timer,&QTimer::timeout,this,[this] {
        QFile failure(impl_->stage+"/failed");
        if(failure.open(QIODevice::ReadOnly)) {
            impl_->timer.stop();impl_->busy=false;
            Q_EMIT failed(QString::fromUtf8(failure.read(8192))+tr("\nUpdate log: %1").arg(logPath()));return;
        }
        if(WaitForSingleObject(impl_->child,0)==WAIT_OBJECT_0 || impl_->handshake.elapsed()>60000) {
            impl_->timer.stop();save(impl_->stage+"/cancel","cancel\n");
            TerminateProcess(impl_->child,1);impl_->busy=false;
            Q_EMIT failed(tr("The portable update helper exited or did not become ready in time.\nUpdate log: %1").arg(logPath()));return;
        }
        QFile ready(impl_->stage+"/ready");
        if(ready.open(QIODevice::ReadOnly) && ready.readAll()=="READY\n") {
            impl_->timer.stop();impl_->handedOff=true;Q_EMIT readyToExit();
        }
    });
#endif
}
PortableUpdateInstaller::~PortableUpdateInstaller() {
    if(!impl_->handedOff) cancel();
    impl_->watcher.waitForFinished();
}
bool PortableUpdateInstaller::busy() const {return impl_->busy;}
QString PortableUpdateInstaller::logPath() const {return impl_->stage+"/portable-update.log";}
void PortableUpdateInstaller::start(const QString& packagePath,const QByteArray& sha256Hex,qint64 expectedSize,const QString& root) {
    if(impl_->busy) {Q_EMIT failed(tr("A portable update is already being prepared."));return;}
    impl_->root=QDir(root).absolutePath();impl_->handedOff=false;
    if(!StorageContext::isInstalled()) {Q_EMIT failed(tr("The active data directory is unavailable."));return;}
    impl_->dataDirectory=StorageContext::current().dataRoot();
    if(impl_->dataDirectory.isEmpty() || !QDir(impl_->dataDirectory).isAbsolute()) {
        Q_EMIT failed(tr("The active data directory is invalid."));return;
    }
#ifdef Q_OS_WIN
    if(impl_->child) {CloseHandle(impl_->child);impl_->child=nullptr;}
#endif
    impl_->stage=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/portable-updates/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkpath(impl_->stage)) {Q_EMIT failed(tr("Cannot create portable update staging directory."));return;}
    impl_->busy=true;impl_->cancelled=std::make_shared<std::atomic_bool>(false);
    const auto stage=impl_->stage,installRoot=impl_->root;const auto cancelFlag=impl_->cancelled;
    impl_->watcher.setFuture(QtConcurrent::run([packagePath,sha256Hex,expectedSize,installRoot,stage,cancelFlag] {
        Outcome outcome;
        try {outcome.package=zzlogg::portable::preparePackage(packagePath,sha256Hex,expectedSize,installRoot,stage,*cancelFlag);}
        catch(const std::exception& error) {outcome.error=QString::fromUtf8(error.what());}
        return outcome;
    }));
}
void PortableUpdateInstaller::cancel() {
    if(!impl_->busy) return;
    // READY permits the host to attempt its final session commit. An explicit
    // cancellation must still revoke that permission if the commit is vetoed.
    // Only the destructor treats a successful handoff as irrevocable.
    const bool wasHandedOff=impl_->handedOff;
    impl_->handedOff=false;
    if(impl_->cancelled) impl_->cancelled->store(true);
    if(!impl_->stage.isEmpty()) save(impl_->stage+"/cancel","cancel\n");
    if(impl_->timer.isActive() || wasHandedOff) {
        impl_->timer.stop();impl_->busy=false;Q_EMIT failed(tr("Portable update cancelled."));
    }
}
