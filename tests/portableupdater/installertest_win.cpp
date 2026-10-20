#include "portableupdateinstaller.h"
#include "installlock_win.h"
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
bool until(const std::function<bool()>& condition) {
    QElapsedTimer elapsed;elapsed.start();
    while(!condition() && elapsed.elapsed()<15000) {
        QCoreApplication::processEvents();QThread::msleep(10);
    }
    return condition();
}
void parentMode() {
    const auto root=QCoreApplication::applicationDirPath();
    const QByteArray manifest="{\"schema\":1,\"files\":[\"ZzLogg.exe\",\".zzlogg-portable\"]}";
    put(root+"/.zzlogg-portable","ZzLogg portable\n");
    put(root+"/.zzlogg-portable-files.json",manifest);
    const auto package=QFileInfo(root).absolutePath()+"/update.zip";
    KZip zip(package);check(zip.open(QIODevice::WriteOnly),"Cannot create ZIP");
    check(zip.writeFile("ZzLogg.exe","replacement must never be installed") &&
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
    QObject::connect(&installer,&PortableUpdateInstaller::readyToExit,[&] {
        ++readyCount;
        // Models commitApplicationExit() vetoing shutdown after READY.
        installer.cancel();
        cancellationReleasedBusy=!installer.busy();
    });
    for(int attempt=1;attempt<=2;++attempt) {
        installer.start(package,hash.result().toHex(),size,root);
        check(until([&] {return readyCount==attempt;}),"Installer did not reach READY");
        const auto stage=QFileInfo(installer.logPath()).absolutePath();
        const bool signalled=QFileInfo::exists(stage+"/cancel");
        // Clean up a broken implementation's helper before reporting failure.
        if(!signalled) put(stage+"/cancel","cancel\n");
        check(until([&] {return QFileInfo::exists(stage+"/failed");}),"Cancelled helper remained active");
        check(cancellationReleasedBusy,"Cancellation after READY must permit a retry");
        check(signalled,"Cancellation after READY must notify the helper");
        check(!QFileInfo::exists(stage+"/backup") && !QFileInfo::exists(stage+"/complete"),
              "Cancelled helper modified the installation");
    }
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        if(app.arguments().contains("--parent")) {parentMode();return 0;}
        check(argc==2,"Expected portable helper path");
        QTemporaryDir directory;check(directory.isValid(),"Cannot create fixture directory");
        const auto root=directory.path()+"/app";check(QDir().mkpath(root),"Cannot create fixture installation");
        check(QFile::copy(app.applicationFilePath(),root+"/ZzLogg.exe"),"Cannot copy parent fixture");
        check(QFile::copy(app.arguments()[1],root+"/ZzLoggPortableUpdate.exe"),"Cannot copy helper");
        QProcess parent;
        parent.start(root+"/ZzLogg.exe",{"--parent"});
        if(!parent.waitForFinished(60000)) {parent.kill();parent.waitForFinished();throw std::runtime_error("Parent fixture timed out");}
        std::cerr<<parent.readAllStandardError().toStdString();
        check(parent.exitStatus()==QProcess::NormalExit && parent.exitCode()==0,"Parent fixture failed");
        std::cout<<"Portable installer READY cancellation and retry passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
