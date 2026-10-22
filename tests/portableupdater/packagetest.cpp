#include "package.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "../testsupport/canonicaltemporarydir.h"
#include <QtTest>
#include <QtEndian>
#include <kzip.h>
using namespace zzlogg::portable;
class PackageTest:public QObject {
    Q_OBJECT
    static void put(const QString& path,const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size());
    }
    static QByteArray digest(const QString& path) {
        QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {};
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&f); return hash.result().toHex();
    }
    static QByteArray manifest(const QStringList& extra={}) {
        QJsonArray files={"ZzLogg.exe",".zzlogg-portable"}; for(const auto& name:extra) files.append(name);
        return QJsonDocument(QJsonObject{{"schema",1},{"files",files}}).toJson();
    }
    static void installed(const QString& root) {
        put(root+"/ZzLogg.exe","old"); put(root+"/.zzlogg-portable","ZzLogg portable\n");
        put(root+"/.zzlogg-portable-files.json",manifest());
        put(root+"/data/config/ZzLogg.ini","settings");
    }
    static void package(const QString& path,const QString& prefix={}, const QString& extra={}) {
        KZip zip(path); QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile(prefix+"ZzLogg.exe","new executable"));
        QVERIFY(zip.writeFile(prefix+".zzlogg-portable","ZzLogg portable\n"));
        QVERIFY(zip.writeFile(prefix+".zzlogg-portable-files.json",manifest(extra.isEmpty()?QStringList{}:QStringList{extra})));
        if(!extra.isEmpty()) QVERIFY(zip.writeFile(prefix+extra,"extra"));
        QVERIFY(zip.close());
    }
private slots:
    void extractsVersionedRoot() {
        CanonicalTemporaryDir dir; const auto root=dir.path()+"/app",zip=dir.path()+"/update.zip",stage=dir.path()+"/stage";
        installed(root); package(zip,"ZzLogg-1.2.3/"); std::atomic_bool cancel{false};
        const auto result=preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,stage,cancel);
        QCOMPARE(result.files.size(),std::size_t(3));
        QFile extracted(stage+"/payload/ZzLogg.exe"); QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(),QByteArray("new executable"));
        QFile original(root+"/ZzLogg.exe"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),QByteArray("old"));
    }
    void refusesHashMismatch() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip"; installed(root);package(zip);
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,QByteArray(64,'0'),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void refusesProtectedData() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip"; installed(root);package(zip,{},"data/config/ZzLogg.ini");
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void refusesCaseAliases() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip"; installed(root);package(zip,{},"zzlogg.exe");
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void refusesTraversalInZipHeaders() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip"; installed(root);package(zip,{},"evil/file.txt");
        QFile source(zip);QVERIFY(source.open(QIODevice::ReadOnly));auto bytes=source.readAll();source.close();
        bytes.replace("evil/file.txt","../escape.txt");put(zip,bytes);
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
        QVERIFY(!QFileInfo::exists(dir.path()+"/escape.txt"));
    }
    void refusesSymbolicLinkEntry() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",path=dir.path()+"/update.zip";installed(root);
        KZip zip(path);QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile("ZzLogg.exe","new executable"));QVERIFY(zip.writeFile(".zzlogg-portable","ZzLogg portable\n"));
        QVERIFY(zip.writeFile(".zzlogg-portable-files.json",manifest({"linked.dll"})));
        QVERIFY(zip.writeSymLink("linked.dll","outside",{},{}));QVERIFY(zip.close());
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(path,digest(path),QFileInfo(path).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void refusesUndeclaredZipFiles() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",path=dir.path()+"/update.zip";installed(root);
        KZip zip(path);QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile("ZzLogg.exe","new executable"));QVERIFY(zip.writeFile(".zzlogg-portable","ZzLogg portable\n"));
        QVERIFY(zip.writeFile(".zzlogg-portable-files.json",manifest()));QVERIFY(zip.writeFile("unowned.txt","unknown"));QVERIFY(zip.close());
        std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(path,digest(path),QFileInfo(path).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void refusesUnknownInstalledFileCollision() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip";installed(root);
        put(root+"/notes.txt","user notes");package(zip,{},"notes.txt");std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
        QFile notes(root+"/notes.txt");QVERIFY(notes.open(QIODevice::ReadOnly));QCOMPARE(notes.readAll(),QByteArray("user notes"));
    }
    void refusesIncorrectUncompressedSize() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip";installed(root);package(zip);
        QFile source(zip);QVERIFY(source.open(QIODevice::ReadOnly));auto bytes=source.readAll();source.close();
        const auto central=bytes.indexOf(QByteArray("PK\x01\x02",4));QVERIFY(central>=0);
        qToLittleEndian<quint32>(999,bytes.data()+central+24);put(zip,bytes);std::atomic_bool cancel{false};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
    }
    void cancellationLeavesInstallation() {
        CanonicalTemporaryDir dir; auto root=dir.path()+"/app",zip=dir.path()+"/update.zip"; installed(root);package(zip);
        std::atomic_bool cancel{true};
        QVERIFY_EXCEPTION_THROWN(preparePackage(zip,digest(zip),QFileInfo(zip).size(),root,dir.path()+"/stage",cancel),std::exception);
        QFile original(root+"/ZzLogg.exe"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),QByteArray("old"));
    }
};
QTEST_GUILESS_MAIN(PackageTest)
#include "packagetest.moc"
