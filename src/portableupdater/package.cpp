#include "package.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QtEndian>
#include <karchive.h>
#include <karchivefile.h>
#include <karchivedirectory.h>
#include <kzip.h>
#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
namespace zzlogg::portable {
namespace {
constexpr qint64 packageLimit=2LL*1024*1024*1024;
constexpr quint64 totalLimit=4ULL*1024*1024*1024;
constexpr quint64 fileLimit=512ULL*1024*1024;
const QString manifestName=QStringLiteral(".zzlogg-portable-files.json");
const QString markerName=QStringLiteral(".zzlogg-portable");
void require(bool value,const QString& message) {if(!value) throw std::runtime_error(message.toStdString());}
void cancelled(const std::atomic_bool& flag) {require(!flag.load(),QStringLiteral("Portable update cancelled."));}
fs::path native(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::u8path(path.toUtf8().constData());
#endif
}
quint16 u16(const QByteArray& data,qsizetype pos) {return qFromLittleEndian<quint16>(data.constData()+pos);}
quint32 u32(const QByteArray& data,qsizetype pos) {return qFromLittleEndian<quint32>(data.constData()+pos);}
struct ZipEntry {QString name;quint32 size;quint32 compressed;quint32 offset;bool directory;};
std::vector<ZipEntry> inspectZip(QFile& file) {
    require(file.size()>=22,QStringLiteral("The portable ZIP is truncated."));
    const auto tailSize=std::min<qint64>(file.size(),65557);
    require(file.seek(file.size()-tailSize),QStringLiteral("Cannot inspect portable ZIP."));
    const auto tail=file.read(tailSize);qsizetype end=-1;
    for(qsizetype i=tail.size()-22;i>=0;--i) if(u32(tail,i)==0x06054b50 && i+22+u16(tail,i+20)==tail.size()) {end=i;break;}
    require(end>=0,QStringLiteral("Missing ZIP directory."));
    const auto count=u16(tail,end+10);const auto centralSize=u32(tail,end+12),offset=u32(tail,end+16);
    require(u16(tail,end+4)==0 && u16(tail,end+6)==0 && u16(tail,end+8)==count && count>0 && count<20000 &&
        centralSize<=16*1024*1024 && quint64(offset)+centralSize==quint64(file.size()-tailSize+end),
        QStringLiteral("Unsupported ZIP64, multipart or oversized portable archive."));
    require(file.seek(offset),QStringLiteral("Invalid ZIP directory offset."));const auto central=file.read(centralSize);
    require(central.size()==centralSize,QStringLiteral("Truncated ZIP directory."));
    std::vector<ZipEntry> entries;QSet<QString> names;quint64 total=0;qsizetype pos=0;
    std::vector<std::pair<quint64,quint64>> ranges;
    for(unsigned i=0;i<count;++i) {
        require(pos+46<=central.size() && u32(central,pos)==0x02014b50,QStringLiteral("Invalid ZIP directory entry."));
        const auto flags=u16(central,pos+8),method=u16(central,pos+10);
        const auto nameSize=u16(central,pos+28),extra=u16(central,pos+30),comment=u16(central,pos+32);
        require(nameSize>0 && pos+46+nameSize+extra+comment<=central.size(),QStringLiteral("Invalid ZIP filename length."));
        const auto raw=central.mid(pos+46,nameSize);const auto name=QString::fromLatin1(raw);
        const auto compressed=u32(central,pos+20),size=u32(central,pos+24),external=u32(central,pos+38),local=u32(central,pos+42);
        const bool directory=name.endsWith('/');const auto path=directory?name.chopped(1):name;
        const auto mode=(external>>16)&0170000;
        require(safePath(path.toStdString()) && !names.contains(path.toLower()),QStringLiteral("Unsafe or duplicate ZIP path: %1").arg(name));
        require(!(flags&1) && (method==0 || method==8) && u16(central,pos+34)==0 &&
            (mode==0 || mode==0100000 || (directory && mode==0040000)) && !(external&0x400),
            QStringLiteral("Encrypted entries, links and special files are not supported: %1").arg(name));
        require(size<=fileLimit && (!directory || size==0),QStringLiteral("Oversized ZIP entry: %1").arg(name));
        total+=size;require(total<=totalLimit,QStringLiteral("The extracted portable package is too large."));
        names.insert(path.toLower());
        require(quint64(local)+30<=offset && file.seek(local),QStringLiteral("Invalid ZIP local entry offset."));
        const auto header=file.read(30);
        require(header.size()==30 && u32(header,0)==0x04034b50 && u16(header,6)==flags && u16(header,8)==method && u16(header,26)==nameSize,
            QStringLiteral("ZIP local header differs from directory."));
        require(file.read(nameSize)==raw,QStringLiteral("ZIP local filename differs from directory."));
        const quint64 localEnd=quint64(local)+30+nameSize+u16(header,28)+compressed;
        require(localEnd<=offset,QStringLiteral("ZIP entry overlaps its directory."));
        ranges.emplace_back(local,localEnd);entries.push_back({name,size,compressed,local,directory});
        pos+=46+nameSize+extra+comment;
    }
    require(pos==central.size(),QStringLiteral("Unexpected ZIP directory data."));
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i) require(ranges[i-1].second<=ranges[i].first,QStringLiteral("Overlapping ZIP entries."));
    return entries;
}
std::vector<std::string> parseManifest(const QByteArray& bytes) {
    require(bytes.size()<=1024*1024,QStringLiteral("Portable ownership manifest is too large."));
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),QStringLiteral("Invalid portable ownership manifest."));
    const auto object=doc.object();
    require(object.value("schema").toDouble()==1.0 && object.value("files").isArray(),QStringLiteral("Unsupported portable ownership manifest."));
    const auto array=object.value("files").toArray();require(array.size()>0 && array.size()<20000,QStringLiteral("Invalid portable file count."));
    std::vector<std::string> paths;QSet<QString> seen;
    for(const auto& value:array) {
        const auto name=value.toString();
        require(value.isString() && safePath(name.toStdString()) && !seen.contains(name.toLower()) && name.toLower()!=manifestName,
            QStringLiteral("Unsafe or duplicate portable ownership path: %1").arg(name));
        seen.insert(name.toLower());paths.push_back(name.toStdString());
    }
    require(seen.contains("zzlogg.exe") && seen.contains(markerName),QStringLiteral("The portable package is missing its application or marker."));
    paths.push_back(manifestName.toStdString());return paths;
}
QByteArray readSmall(const QString& path) {
    QFile file(path);require(file.open(QIODevice::ReadOnly) && file.size()<=1024*1024,QStringLiteral("Cannot read portable metadata: %1").arg(path));
    return file.readAll();
}
QByteArray hashDevice(QIODevice& file,const std::atomic_bool& flag) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {cancelled(flag);const auto block=file.read(1024*1024);require(!block.isEmpty() || file.atEnd(),QStringLiteral("Cannot hash package data."));hash.addData(block);}
    return hash.result().toHex();
}
}
PreparedPackage preparePackage(const QString& zipPath,const QByteArray& expectedSha256,qint64 expectedSize,
    const QString& installRoot,const QString& stage,const std::atomic_bool& flag) {
    cancelled(flag);
    require(expectedSize>0 && expectedSize<=packageLimit && expectedSha256.size()==64 &&
        QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(QString::fromLatin1(expectedSha256)).hasMatch(),QStringLiteral("Invalid signed portable package identity."));
    const auto root=native(installRoot),staging=native(stage);
    validateTreePath(root,manifestName.toStdString());validateTreePath(root,markerName.toStdString());
    require(readSmall(installRoot+'/'+markerName)=="ZzLogg portable\n",QStringLiteral("This installation has no valid portable package marker."));
    const auto oldManifest=readSmall(installRoot+'/'+manifestName);
    PreparedPackage result;result.oldFiles=parseManifest(oldManifest);
    result.oldManifestSha256=QCryptographicHash::hash(oldManifest,QCryptographicHash::Sha256).toHex();
    require(QDir().mkpath(stage),QStringLiteral("Cannot create staging directory."));validateTreePath(staging,"package.zip");
    const auto canonicalRoot=QFileInfo(installRoot).canonicalFilePath(),canonicalStage=QFileInfo(stage).canonicalFilePath();
    require(!canonicalRoot.isEmpty() && !canonicalStage.isEmpty() &&
        canonicalRoot.compare(canonicalStage,Qt::CaseInsensitive)!=0 &&
        !canonicalStage.startsWith(canonicalRoot+'/',Qt::CaseInsensitive),QStringLiteral("Staging must be outside the portable program directory."));
    QTemporaryFile writable(installRoot+"/.zzlogg-update-write-XXXXXX");
    require(writable.open(),QStringLiteral("The portable directory is not writable. Move the complete portable folder to a writable local drive."));
    writable.close();writable.remove();
    QFile source(zipPath);require(source.open(QIODevice::ReadOnly) && source.size()==expectedSize,QStringLiteral("Downloaded portable package size changed."));
    // A private verified snapshot prevents cache replacement from changing the
    // archive that KZip reads during extraction.
    QSaveFile snapshot(stage+"/package.zip");require(snapshot.open(QIODevice::WriteOnly),QStringLiteral("Cannot create package snapshot."));
    QCryptographicHash hash(QCryptographicHash::Sha256);qint64 copied=0;
    while(!source.atEnd()) {
        cancelled(flag);const auto bytes=source.read(1024*1024);
        require(!bytes.isEmpty() || source.atEnd(),QStringLiteral("Cannot read downloaded portable package."));
        copied+=bytes.size();require(copied<=expectedSize && snapshot.write(bytes)==bytes.size(),QStringLiteral("Cannot stage the portable package."));hash.addData(bytes);
    }
    require(copied==expectedSize && hash.result().toHex()==expectedSha256,QStringLiteral("Downloaded portable package SHA-256 verification failed."));
    require(snapshot.commit(),QStringLiteral("Cannot finish package snapshot."));
    QFile archiveFile(stage+"/package.zip");require(archiveFile.open(QIODevice::ReadOnly),QStringLiteral("Cannot read package snapshot."));
    auto entries=inspectZip(archiveFile);QString prefix;
    const auto manifestEntry=std::find_if(entries.begin(),entries.end(),[](const auto& entry){return !entry.directory && entry.name==manifestName;});
    if(manifestEntry==entries.end()) {
        const auto first=entries.front().name.section('/',0,0);
        require(QRegularExpression(QStringLiteral("^ZzLogg[-_][A-Za-z0-9._-]+$"),QRegularExpression::CaseInsensitiveOption).match(first).hasMatch(),
            QStringLiteral("Unexpected portable package root directory."));
        prefix=first+'/';
        for(const auto& entry:entries) require(entry.name.startsWith(prefix),QStringLiteral("Portable ZIP has multiple package roots."));
    }
    require(archiveFile.seek(0),QStringLiteral("Cannot reopen portable ZIP."));KZip zip(&archiveFile);
    require(zip.open(QIODevice::ReadOnly),QStringLiteral("Cannot open portable ZIP."));
    const auto* metadata=zip.directory()->entry(prefix+manifestName);
    require(metadata && metadata->isFile() && metadata->symLinkTarget().isEmpty(),QStringLiteral("Portable package ownership manifest is missing."));
    const auto* metadataFile=static_cast<const KArchiveFile*>(metadata);
    require(metadataFile->size()<=1024*1024,QStringLiteral("Portable ownership manifest is too large."));
    std::unique_ptr<QIODevice> metadataInput(metadataFile->createDevice());
    require(metadataInput && metadataInput->isOpen(),QStringLiteral("Cannot read portable ownership manifest."));
    const auto metadataBytes=metadataInput->read(1024*1024+1);
    require(metadataBytes.size()==metadataFile->size() && metadataInput->atEnd(),QStringLiteral("Portable manifest exceeds its declared size."));
    metadataInput.reset();
    const auto owned=parseManifest(metadataBytes);QSet<QString> ownedSet,archiveSet;
    for(const auto& path:owned) ownedSet.insert(QString::fromStdString(path));
    quint64 total=0;
    for(auto& entry:entries) {
        entry.name.remove(0,prefix.size()); if(entry.directory) continue;
        require(ownedSet.contains(entry.name),QStringLiteral("ZIP file is absent from ownership manifest: %1").arg(entry.name));
        archiveSet.insert(entry.name);total+=entry.size;
    }
    require(ownedSet==archiveSet,QStringLiteral("Ownership manifest and ZIP contents do not match."));
    QStorageInfo space(stage);require(!space.isValid() || space.bytesAvailable()>qint64(total*2+64*1024*1024),QStringLiteral("Insufficient disk space for portable update and backup."));
    require(QDir().mkpath(stage+"/payload"),QStringLiteral("Cannot create extracted payload directory."));
    for(const auto& entry:entries) {
        cancelled(flag);if(entry.directory) continue;
        const auto* member=zip.directory()->entry(prefix+entry.name);
        require(member && member->isFile() && member->symLinkTarget().isEmpty(),QStringLiteral("Invalid portable ZIP member."));
        const auto* file=static_cast<const KArchiveFile*>(member);
        require(file->size()==entry.size,QStringLiteral("ZIP member size differs from its directory."));
        validateTreePath(staging/"payload",entry.name.toStdString());
        const auto outputPath=stage+"/payload/"+entry.name;require(QDir().mkpath(QFileInfo(outputPath).absolutePath()),QStringLiteral("Cannot create package subdirectory."));
        std::unique_ptr<QIODevice> input(file->createDevice());require(input && input->isOpen(),QStringLiteral("Cannot decompress ZIP member."));
        QSaveFile output(outputPath);require(output.open(QIODevice::WriteOnly),QStringLiteral("Cannot create staged program file."));
        QCryptographicHash memberHash(QCryptographicHash::Sha256);quint64 written=0;
        while(!input->atEnd()) {
            cancelled(flag);const auto bytes=input->read(1024*1024);written+=quint64(bytes.size());
            require((!bytes.isEmpty() || input->atEnd()) && written<=entry.size && output.write(bytes)==bytes.size(),QStringLiteral("ZIP extraction failed or exceeded declared size."));memberHash.addData(bytes);
        }
        require(written==entry.size && output.commit(),QStringLiteral("Incomplete staged program file."));
        result.files.push_back({entry.name.toStdString(),written,memberHash.result().toHex().toStdString()});
    }
    require(readSmall(stage+"/payload/"+markerName)=="ZzLogg portable\n",QStringLiteral("Invalid portable package marker."));
    zip.close(); require(archiveFile.openMode()!=QIODevice::NotOpen || archiveFile.open(QIODevice::ReadOnly),QStringLiteral("Cannot recheck package snapshot."));
    require(archiveFile.seek(0) && hashDevice(archiveFile,flag)==expectedSha256,QStringLiteral("Package snapshot changed during extraction."));
    const Hasher stagedHash=[&flag](const fs::path& path) {
#ifdef Q_OS_WIN
        QFile file(QString::fromStdWString(path.wstring()));
#else
        QFile file(QString::fromUtf8(path.u8string().c_str()));
#endif
        require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot verify staged file."));return hashDevice(file,flag).toStdString();
    };
    validateTransaction(root,staging/"payload",result.oldFiles,result.files,stagedHash);
    return result;
}
}
