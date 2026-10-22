#include "portableupdateinstaller.h"
#include "installlock_win.h"
#include "storagecontext.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <kzip.h>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void put(const QString& path,const QByteArray& bytes) {
    QFile file(path);check(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(),"Cannot write fixture");
}
QByteArray read(const QString& path) {
    QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();
}
bool until(const std::function<bool()>& condition,int timeoutMs=15000) {
    QElapsedTimer elapsed;elapsed.start();
    while(!condition() && elapsed.elapsed()<timeoutMs) {
        QCoreApplication::processEvents();QThread::msleep(10);
    }
    return condition();
}
void parentMode(bool commit,const QString& replacement) {
    const auto root=QCoreApplication::applicationDirPath();
    const auto dataRoot=QDir(root).filePath("custom data root");
    check(QDir().mkpath(dataRoot),"Cannot create custom data root");
    check(StorageContext::install({StorageMode::CustomDirectory,dataRoot,{},true}),
          "Cannot install active storage context");
    const QByteArray manifest="{\"schema\":1,\"files\":[\"ZzLogg.exe\",\".zzlogg-portable\"]}";
    put(root+"/.zzlogg-portable","ZzLogg portable\n");
    put(root+"/.zzlogg-portable-files.json",manifest);
    const auto package=QFileInfo(root).absolutePath()+"/update.zip";
    KZip zip(package);check(zip.open(QIODevice::WriteOnly),"Cannot create ZIP");
    const auto image=commit?read(replacement):QByteArray("replacement must never be installed");
    check(zip.writeFile("ZzLogg.exe",image) &&
          zip.writeFile(".zzlogg-portable","ZzLogg portable\n") &&
          zip.writeFile(".zzlogg-portable-files.json",manifest) && zip.close(),"Cannot finish ZIP");
    QFile source(package);check(source.open(QIODevice::ReadOnly),"Cannot read ZIP");
    QCryptographicHash hash(QCryptographicHash::Sha256);check(hash.addData(&source),"Cannot hash ZIP");
    const auto size=source.size();source.close();
    zzlogg::updater::InstallLock reservation;
    check(reservation.acquire(QDir::toNativeSeparators(root).toStdWString())==zzlogg::updater::InstallLockError::None,
          "Cannot reserve fixture installation");

    PortableUpdateInstaller installer;
    int readyCount=0;
    bool cancellationReleasedBusy=false;
    QString failure;
    QObject::connect(&installer,&PortableUpdateInstaller::failed,[&](const QString& error) {failure=error;});
    QObject::connect(&installer,&PortableUpdateInstaller::readyToExit,[&] {
        ++readyCount;
        if(!commit) {
            // Models commitApplicationExit() vetoing shutdown after READY.
            installer.cancel();
            cancellationReleasedBusy=!installer.busy();
        }
    });
    for(int attempt=1;attempt<=(commit?1:2);++attempt) {
        installer.start(package,hash.result().toHex(),size,root);
        check(until([&] {return readyCount==attempt || !failure.isEmpty();}),
              "Installer did not reach READY");
        if(readyCount!=attempt)
            throw std::runtime_error("Installer failed before READY: "+failure.toStdString());
        const auto stage=QFileInfo(installer.logPath()).absolutePath();
        if(commit) {
            check(!QFileInfo::exists(stage+"/backup") && !QFileInfo::exists(stage+"/complete"),
                  "Helper modified installation before parent exit");
            put(QFileInfo(root).absolutePath()+"/stage-path.txt",stage.toUtf8());
            return;
        }
        const bool signalled=QFileInfo::exists(stage+"/cancel");
        // Clean up a broken implementation's helper before reporting failure.
        if(!signalled) put(stage+"/cancel","cancel\n");
        check(until([&] {return QFileInfo::exists(stage+"/failed");}),"Cancelled helper remained active");
        check(cancellationReleasedBusy,"Cancellation after READY must permit a retry");
        check(signalled,"Cancellation after READY must notify the helper");
        check(!QFileInfo::exists(stage+"/backup") && !QFileInfo::exists(stage+"/complete"),
              "Cancelled helper modified the installation");
        failure.clear();
    }
}
void runScenario(const QString& base,const QString& helper,const QString& replacement,bool commit) {
    const auto root=base+(commit?"/success/app":"/cancel/app");
    check(QDir().mkpath(root),"Cannot create fixture installation");
    check(QFile::copy(QCoreApplication::applicationFilePath(),root+"/ZzLogg.exe"),"Cannot copy parent fixture");
    check(QFile::copy(helper,root+"/ZzLoggPortableUpdate.exe"),"Cannot copy helper");
    QProcess parent;
    parent.start(root+"/ZzLogg.exe",{"--parent",commit?"success":"cancel",replacement});
    if(!parent.waitForFinished(60000)) {parent.kill();parent.waitForFinished();throw std::runtime_error("Parent fixture timed out");}
    std::cerr<<parent.readAllStandardError().toStdString();
    check(parent.exitStatus()==QProcess::NormalExit && parent.exitCode()==0,"Parent fixture failed");
    if(!commit) return;
    const auto stage=QString::fromUtf8(read(QFileInfo(root).absolutePath()+"/stage-path.txt"));
    check(until([&] {return QFileInfo::exists(stage+"/complete") || QFileInfo::exists(stage+"/failed");},30000),
          "Helper did not finish after parent exit");
    if(QFileInfo::exists(stage+"/failed"))
        throw std::runtime_error("Helper failed after parent exit: "+read(stage+"/failed").toStdString());
    check(until([&] {return QFileInfo::exists(root+"/restarted");}),"Replacement application did not restart");
    const auto expected=QDir::toNativeSeparators(QDir(root).filePath("custom data root")).toUtf8();
    check(read(root+"/restarted")==expected,"Restart did not receive the active custom data root");
    check(read(root+"/ZzLogg.exe")==read(replacement),"Installed executable differs from replacement");
    check(QFileInfo::exists(stage+"/backup/ZzLogg.exe"),"Original executable was not backed up");
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        if(app.arguments().contains("--parent")) {
            check(argc==4,"Expected parent mode and replacement fixture");
            parentMode(app.arguments()[2]=="success",app.arguments()[3]);return 0;
        }
        check(argc==3,"Expected portable helper and replacement fixture paths");
        QTemporaryDir directory;check(directory.isValid(),"Cannot create fixture directory");
        runScenario(directory.path(),app.arguments()[1],app.arguments()[2],false);
        runScenario(directory.path(),app.arguments()[1],app.arguments()[2],true);
        std::cout<<"Portable installer cancellation, retry and restart passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
