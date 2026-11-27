/* This file is part of the KDE libraries
   Copyright (C) 2000-2005 David Faure <faure@kde.org>
   Copyright (C) 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.cpp by Roberto Teixeira <maragato@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "karchive.h"
#include "karchive_p.h"
#include "klimitediodevice_p.h"
#include "loggingcategory.h"

#include <qplatformdefs.h> // QT_STATBUF, QT_LSTAT

#include <QStack>
#include <QMap>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <memory>
#include <limits>
#include <QSaveFile>
#include <QSet>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef Q_OS_UNIX
#include <grp.h>
#include <pwd.h>
#include <limits.h>  // PATH_MAX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h> // DWORD, GetUserNameW
#endif // Q_OS_WIN

////////////////////////////////////////////////////////////////////////
/////////////////// KArchiveDirectoryPrivate ///////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveDirectoryPrivate
{
public:
    KArchiveDirectoryPrivate(KArchiveDirectory *parent) : q(parent)
    {
    }

    ~KArchiveDirectoryPrivate()
    {
        qDeleteAll(entries);
    }

    KArchiveDirectoryPrivate(const KArchiveDirectoryPrivate &) = delete;
    KArchiveDirectoryPrivate &operator=(const KArchiveDirectoryPrivate &) = delete;

    static KArchiveDirectoryPrivate *get(KArchiveDirectory *directory)
    {
        return directory->d;
    }

    // Returns in containingDirectory the directory that actually contains the returned entry
    const KArchiveEntry *entry(const QString &_name, KArchiveDirectory **containingDirectory) const
    {
        *containingDirectory = q;

        QString name = QDir::cleanPath(_name);
        int pos = name.indexOf(QLatin1Char('/'));
        if (pos == 0) { // ouch absolute path (see also KArchive::findOrCreate)
            if (name.length() > 1) {
                name = name.mid(1);   // remove leading slash
                pos = name.indexOf(QLatin1Char('/'));   // look again
            } else { // "/"
                return q;
            }
        }
        // trailing slash ? -> remove
        if (pos != -1 && pos == name.length() - 1) {
            name = name.left(pos);
            pos = name.indexOf(QLatin1Char('/'));   // look again
        }
        if (pos != -1) {
            const QString left = name.left(pos);
            const QString right = name.mid(pos + 1);

            //qCDebug(KArchiveLog) << "left=" << left << "right=" << right;

            KArchiveEntry *e = entries.value(left);
            if (!e || !e->isDirectory()) {
                return nullptr;
            }
            *containingDirectory = static_cast<KArchiveDirectory *>(e);
            return (*containingDirectory)->d->entry(right, containingDirectory);
        }

        return entries.value(name);
    }

    KArchiveDirectory *q;
    QHash<QString, KArchiveEntry *> entries;
};

////////////////////////////////////////////////////////////////////////
/////////////////////////// KArchive ///////////////////////////////////
////////////////////////////////////////////////////////////////////////

KArchive::KArchive(const QString &fileName)
    : d(new KArchivePrivate(this))
{
    if (fileName.isEmpty()) {
        qCWarning(KArchiveLog) << "KArchive: No file name specified";
    }
    d->fileName = fileName;
    // This constructor leaves the device set to 0.
    // This is for the use of QSaveFile, see open().
}

KArchive::KArchive(QIODevice *dev)
    : d(new KArchivePrivate(this))
{
    if (!dev) {
        qCWarning(KArchiveLog) << "KArchive: Null device specified";
    }
    d->dev = dev;
}

KArchive::~KArchive()
{
    Q_ASSERT(!isOpen()); // the derived class destructor must have closed already
    delete d;
}

bool KArchive::open(QIODevice::OpenMode mode)
{
    Q_ASSERT(mode != QIODevice::NotOpen);

    if (isOpen()) {
        close();
    }

    if (!d->fileName.isEmpty()) {
        Q_ASSERT(!d->dev);
        if (!createDevice(mode)) {
            return false;
        }
    }

    if (!d->dev) {
        setErrorString(tr("No filename or device was specified"));
        return false;
    }

    if (!d->dev->isOpen() && !d->dev->open(mode)) {
        setErrorString(tr("Could not set device mode to %1").arg(static_cast<int>(mode)));
        return false;
    }

    d->mode = mode;

    Q_ASSERT(!d->rootDir);
    d->rootDir = nullptr;

    return openArchive(mode);
}

bool KArchive::createDevice(QIODevice::OpenMode mode)
{
    switch (mode) {
    case QIODevice::WriteOnly:
        if (!d->fileName.isEmpty()) {
            // The use of QSaveFile can't be done in the ctor (no mode known yet)
            //qCDebug(KArchiveLog) << "Writing to a file using QSaveFile";
            d->saveFile = new QSaveFile(d->fileName);
#ifdef Q_OS_ANDROID
            // we cannot rename on to Android content: URLs
            if (d->fileName.startsWith(QLatin1String("content://"))) {
                d->saveFile->setDirectWriteFallback(true);
            }
#endif
            if (!d->saveFile->open(QIODevice::WriteOnly)) {
                setErrorString(
                    tr("QSaveFile creation for %1 failed: %2")
                        .arg(d->fileName, d->saveFile->errorString()));

                delete d->saveFile;
                d->saveFile = nullptr;
                return false;
            }
            d->dev = d->saveFile;
            Q_ASSERT(d->dev);
        }
        break;
    case QIODevice::ReadOnly:
    case QIODevice::ReadWrite:
        // ReadWrite mode still uses QFile for now; we'd need to copy to the tempfile, in fact.
        if (!d->fileName.isEmpty()) {
            d->dev = new QFile(d->fileName);
            d->deviceOwned = true;
        }
        break; // continued below
    default:
        setErrorString(tr("Unsupported mode %1").arg(static_cast<int>(d->mode)));
        return false;
    }
    return true;
}

bool KArchive::close()
{
    if (!isOpen()) {
        setErrorString(tr("Archive already closed"));
        return false;    // already closed (return false or true? arguable...)
    }

    // moved by holger to allow kzip to write the zip central dir
    // to the file in closeArchive()
    // DF: added d->dev so that we skip closeArchive if saving aborted.
    bool closeSucceeded = true;
    if (d->dev) {
        closeSucceeded = closeArchive();
        if (d->mode == QIODevice::WriteOnly && !closeSucceeded) {
            d->abortWriting();
        }
    }

    if (d->dev && d->dev != d->saveFile) {
        d->dev->close();
    }

    // if d->saveFile is not null then it is equal to d->dev.
    if (d->saveFile) {
        closeSucceeded = d->saveFile->commit();
        delete d->saveFile;
        d->saveFile = nullptr;
    }
    if (d->deviceOwned) {
        delete d->dev; // we created it ourselves in open()
    }

    delete d->rootDir;
    d->rootDir = nullptr;
    d->mode = QIODevice::NotOpen;
    d->dev = nullptr;
    return closeSucceeded;
}

QString KArchive::errorString() const
{
    return d->errorStr;
}

const KArchiveDirectory *KArchive::directory() const
{
    // rootDir isn't const so that parsing-on-demand is possible
    return const_cast<KArchive *>(this)->rootDir();
}

bool KArchive::addLocalFile(const QString &fileName, const QString &destName)
{
    QFileInfo fileInfo(fileName);
    if (!fileInfo.isFile() && !fileInfo.isSymLink()) {
        setErrorString(
            tr("%1 doesn't exist or is not a regular file.")
            .arg(fileName));
        return false;
    }

#if defined(Q_OS_UNIX)
#define STAT_METHOD QT_LSTAT
#else
#define STAT_METHOD QT_STAT
#endif
    QT_STATBUF fi;
    if (STAT_METHOD(QFile::encodeName(fileName).constData(), &fi) == -1) {
        setErrorString(
            tr("Failed accessing the file %1 for adding to the archive. The error was: %2")
            .arg(fileName)
            .arg(QLatin1String{strerror(errno)}));
        return false;
    }

    if (fileInfo.isSymLink()) {
        QString symLinkTarget;
        // Do NOT use fileInfo.symLinkTarget() for unix symlinks!
        // It returns the -full- path to the target, while we want the target string "as is".
#if defined(Q_OS_UNIX) && !defined(Q_OS_OS2EMX)
        const QByteArray encodedFileName = QFile::encodeName(fileName);
        QByteArray s;
#if defined(PATH_MAX)
        s.resize(PATH_MAX + 1);
#else
        int path_max = pathconf(encodedFileName.data(), _PC_PATH_MAX);
        if (path_max <= 0) {
            path_max = 4096;
        }
        s.resize(path_max);
#endif
        int len = readlink(encodedFileName.data(), s.data(), s.size() - 1);
        if (len >= 0) {
            s[len] = '\0';
            symLinkTarget = QFile::decodeName(s.constData());
        }
#endif
        if (symLinkTarget.isEmpty()) { // Mac or Windows
            symLinkTarget = fileInfo.symLinkTarget();
        }
#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
        return writeSymLink(destName, symLinkTarget, fileInfo.owner(),
                            fileInfo.group(), fi.st_mode, fileInfo.lastRead(), fileInfo.lastModified(),
                            fileInfo.birthTime());
#else
        return writeSymLink(destName, symLinkTarget, fileInfo.owner(),
                            fileInfo.group(), fi.st_mode, fileInfo.lastRead(), fileInfo.lastModified(),
                            fileInfo.created());
#endif
    }/*end if*/

    qint64 size = fileInfo.size();

    // the file must be opened before prepareWriting is called, otherwise
    // if the opening fails, no content will follow the already written
    // header and the tar file is effectively f*cked up
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        setErrorString(
            tr("Couldn't open file %1: %2")
            .arg(fileName, file.errorString()));
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
    if (!prepareWriting(destName, fileInfo.owner(), fileInfo.group(), size,
                        fi.st_mode, fileInfo.lastRead(), fileInfo.lastModified(), fileInfo.birthTime())) {
        //qCWarning(KArchiveLog) << " prepareWriting" << destName << "failed";
        return false;
    }
#else
    if (!prepareWriting(destName, fileInfo.owner(), fileInfo.group(), size,
                        fi.st_mode, fileInfo.lastRead(), fileInfo.lastModified(), fileInfo.created())) {
        //qCWarning(KArchiveLog) << " prepareWriting" << destName << "failed";
        return false;
    }
#endif

    // Read and write data in chunks to minimize memory usage
    QByteArray array;
    array.resize(int(qMin(qint64(1024 * 1024), size)));
    qint64 n;
    qint64 total = 0;
    while ((n = file.read(array.data(), array.size())) > 0) {
        if (!writeData(array.data(), n)) {
            //qCWarning(KArchiveLog) << "writeData failed";
            return false;
        }
        total += n;
    }
    Q_ASSERT(total == size);

    if (!finishWriting(size)) {
        //qCWarning(KArchiveLog) << "finishWriting failed";
        return false;
    }
    return true;
}

bool KArchive::addLocalDirectory(const QString &path, const QString &destName)
{
    QDir dir(path);
    if (!dir.exists()) {
        setErrorString(
            tr("Directory %1 does not exist")
            .arg(path));
        return false;
    }
    dir.setFilter(dir.filter() | QDir::Hidden);
    const QStringList files = dir.entryList();
    for (QStringList::ConstIterator it = files.begin(); it != files.end(); ++it) {
        if (*it != QLatin1String(".") && *it != QLatin1String("..")) {
            QString fileName = path + QLatin1Char('/') + *it;
//            qCDebug(KArchiveLog) << "storing " << fileName;
            QString dest = destName.isEmpty() ? *it : (destName + QLatin1Char('/') + *it);
            QFileInfo fileInfo(fileName);

            if (fileInfo.isFile() || fileInfo.isSymLink()) {
                addLocalFile(fileName, dest);
            } else if (fileInfo.isDir()) {
                addLocalDirectory(fileName, dest);
            }
            // We omit sockets
        }
    }
    return true;
}

bool KArchive::writeFile(const QString &name, const QByteArray &data,
                         mode_t perm,
                         const QString &user, const QString &group, const QDateTime &atime,
                         const QDateTime &mtime, const QDateTime &ctime)
{
    const qint64 size = data.size();
    if (!prepareWriting(name, user, group, size, perm, atime, mtime, ctime)) {
        //qCWarning(KArchiveLog) << "prepareWriting failed";
        return false;
    }

    // Write data
    // Note: if data is null, don't call write, it would terminate the KCompressionDevice
    if (data.constData() && size && !writeData(data.constData(), size)) {
        //qCWarning(KArchiveLog) << "writeData failed";
        return false;
    }

    if (!finishWriting(size)) {
        //qCWarning(KArchiveLog) << "finishWriting failed";
        return false;
    }
    return true;
}

bool KArchive::writeData(const char *data, qint64 size)
{
    bool ok = device()->write(data, size) == size;
    if (!ok) {
        setErrorString(
            tr("Writing failed: %1")
            .arg(device()->errorString()));
        d->abortWriting();
    }
    return ok;
}

// The writeDir -> doWriteDir pattern allows to avoid propagating the default
// values into all virtual methods of subclasses, and it allows more extensibility:
// if a new argument is needed, we can add a writeDir overload which stores the
// additional argument in the d pointer, and doWriteDir reimplementations can fetch
// it from there.

bool KArchive::writeDir(const QString &name, const QString &user, const QString &group,
                        mode_t perm, const QDateTime &atime,
                        const QDateTime &mtime, const QDateTime &ctime)
{
    return doWriteDir(name, user, group, perm | 040000, atime, mtime, ctime);
}

bool KArchive::writeSymLink(const QString &name, const QString &target,
                            const QString &user, const QString &group,
                            mode_t perm, const QDateTime &atime,
                            const QDateTime &mtime, const QDateTime &ctime)
{
    return doWriteSymLink(name, target, user, group, perm, atime, mtime, ctime);
}

bool KArchive::prepareWriting(const QString &name, const QString &user,
                              const QString &group, qint64 size,
                              mode_t perm, const QDateTime &atime,
                              const QDateTime &mtime, const QDateTime &ctime)
{
    bool ok = doPrepareWriting(name, user, group, size, perm, atime, mtime, ctime);
    if (!ok) {
        d->abortWriting();
    }
    return ok;
}

bool KArchive::finishWriting(qint64 size)
{
    return doFinishWriting(size);
}

void KArchive::setErrorString(const QString &errorStr)
{
    d->errorStr = errorStr;
}

static QString getCurrentUserName()
{
#if defined(Q_OS_UNIX)
    struct passwd *pw = getpwuid(getuid());
    return pw ? QFile::decodeName(pw->pw_name) : QString::number(getuid());
#elif defined(Q_OS_WIN)
    wchar_t buffer[255];
    DWORD size = 255;
    bool ok = GetUserNameW(buffer, &size);
    if (!ok) {
        return QString();
    }
    return QString::fromWCharArray(buffer);
#else
    return QString();
#endif
}

static QString getCurrentGroupName()
{
#if defined(Q_OS_UNIX)
    struct group *grp = getgrgid(getgid());
    return grp ? QFile::decodeName(grp->gr_name) : QString::number(getgid());
#elif defined(Q_OS_WIN)
    return QString();
#else
    return QString();
#endif
}

KArchiveDirectory *KArchive::rootDir()
{
    if (!d->rootDir) {
        //qCDebug(KArchiveLog) << "Making root dir ";
        QString username = ::getCurrentUserName();
        QString groupname = ::getCurrentGroupName();

        d->rootDir = new KArchiveDirectory(this, QStringLiteral("/"), int(0777 + S_IFDIR), QDateTime(), username, groupname, QString());
    }
    return d->rootDir;
}

KArchiveDirectory *KArchive::findOrCreate(const QString &path)
{
    return d->findOrCreate(path, 0 /*recursionCounter*/);
}

KArchiveDirectory *KArchivePrivate::findOrCreate(const QString &path, int recursionCounter)
{
    // Check we're not in a path that is ultra deep, this is most probably fine since PATH_MAX on Linux
    // is defined as 4096, so even on /a/a/a/a/a/a 2500 recursions puts us over that limit
    // an ultra deep recursion will makes us crash due to not enough stack. Tests show that 1MB stack
    // (default on Linux seems to be 8MB) gives us up to around 4000 recursions
    if (recursionCounter > 2500) {
        qCWarning(KArchiveLog) << "path recursion limit exceeded, bailing out";
        return nullptr;
    }
    //qCDebug(KArchiveLog) << path;
    if (path.isEmpty() || path == QLatin1String("/") || path == QLatin1String(".")) { // root dir => found
        //qCDebug(KArchiveLog) << "returning rootdir";
        return q->rootDir();
    }
    // Important note : for tar files containing absolute paths
    // (i.e. beginning with "/"), this means the leading "/" will
    // be removed (no KDirectory for it), which is exactly the way
    // the "tar" program works (though it displays a warning about it)
    // See also KArchiveDirectory::entry().

    // Already created ? => found
    KArchiveDirectory *existingEntryParentDirectory;
    const KArchiveEntry *existingEntry = KArchiveDirectoryPrivate::get(q->rootDir())->entry(path, &existingEntryParentDirectory);
    if (existingEntry) {
        if (existingEntry->isDirectory())
            //qCDebug(KArchiveLog) << "found it";
        {
            const KArchiveDirectory *dir = static_cast<const KArchiveDirectory *>(existingEntry);
            return const_cast<KArchiveDirectory *>(dir);
        } else {
            const KArchiveFile *file = static_cast<const KArchiveFile *>(existingEntry);
            if (file->size() > 0) {
                qCWarning(KArchiveLog) << path << "is normal file, but there are file paths in the archive assuming it is a directory, bailing out";
                return nullptr;
            }

            qCDebug(KArchiveLog) << path << " is an empty file, assuming it is actually a directory and replacing";
            KArchiveEntry *myEntry = const_cast<KArchiveEntry*>(existingEntry);
            existingEntryParentDirectory->removeEntry(myEntry);
            delete myEntry;
        }
    }

    // Otherwise go up and try again
    int pos = path.lastIndexOf(QLatin1Char('/'));
    KArchiveDirectory *parent;
    QString dirname;
    if (pos == -1) { // no more slash => create in root dir
        parent =  q->rootDir();
        dirname = path;
    } else {
        QString left = path.left(pos);
        dirname = path.mid(pos + 1);
        parent = findOrCreate(left, recursionCounter + 1);   // recursive call... until we find an existing dir.
    }

    if (!parent) {
        return nullptr;
    }

    //qCDebug(KArchiveLog) << "found parent " << parent->name() << " adding " << dirname << " to ensure " << path;
    // Found -> add the missing piece
    KArchiveDirectory *e = new KArchiveDirectory(q, dirname, rootDir->permissions(),
                                                 rootDir->date(), rootDir->user(),
                                                 rootDir->group(), QString());
    if (parent->addEntryV2(e)) {
        return e; // now a directory to <path> exists
    } else {
        return nullptr;
    }
}

void KArchive::setDevice(QIODevice *dev)
{
    if (d->deviceOwned) {
        delete d->dev;
    }
    d->dev = dev;
    d->deviceOwned = false;
}

void KArchive::setRootDir(KArchiveDirectory *rootDir)
{
    Q_ASSERT(!d->rootDir);   // Call setRootDir only once during parsing please ;)
    delete d->rootDir;       // but if it happens, don't leak
    d->rootDir = rootDir;
}

QIODevice::OpenMode KArchive::mode() const
{
    return d->mode;
}

QIODevice *KArchive::device() const
{
    return d->dev;
}

bool KArchive::isOpen() const
{
    return d->mode != QIODevice::NotOpen;
}

QString KArchive::fileName() const
{
    return d->fileName;
}

void KArchivePrivate::abortWriting()
{
    if (saveFile) {
        saveFile->cancelWriting();
        delete saveFile;
        saveFile = nullptr;
        dev = nullptr;
    }
}

// this is a hacky wrapper to check if time_t value is invalid
QDateTime KArchivePrivate::time_tToDateTime(uint time_t)
{
    if (time_t == uint(-1)) {
        return QDateTime();
    }
    return QDateTime::fromSecsSinceEpoch(time_t);
}

////////////////////////////////////////////////////////////////////////
/////////////////////// KArchiveEntry //////////////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveEntryPrivate
{
public:
    KArchiveEntryPrivate(KArchive *_archive, const QString &_name, int _access,
                         const QDateTime &_date, const QString &_user, const QString &_group,
                         const QString &_symlink)
        : name(_name)
        , date(_date)
        , access(_access)
        , user(_user)
        , group(_group)
        , symlink(_symlink)
        , archive(_archive)
    {
    }
    QString name;
    QDateTime date;
    mode_t access;
    QString user;
    QString group;
    QString symlink;
    KArchive *archive;
};

KArchiveEntry::KArchiveEntry(KArchive *t, const QString &name, int access, const QDateTime &date,
                             const QString &user, const QString &group, const
                             QString &symlink)
    : d(new KArchiveEntryPrivate(t, name, access, date, user, group, symlink))
{
}

KArchiveEntry::~KArchiveEntry()
{
    delete d;
}

QDateTime KArchiveEntry::date() const
{
    return d->date;
}

QString KArchiveEntry::name() const
{
    return d->name;
}

mode_t KArchiveEntry::permissions() const
{
    return d->access;
}

QString KArchiveEntry::user() const
{
    return d->user;
}

QString KArchiveEntry::group() const
{
    return d->group;
}

QString KArchiveEntry::symLinkTarget() const
{
    return d->symlink;
}

bool KArchiveEntry::isFile() const
{
    return false;
}

bool KArchiveEntry::isDirectory() const
{
    return false;
}

KArchive *KArchiveEntry::archive() const
{
    return d->archive;
}

////////////////////////////////////////////////////////////////////////
/////////////////////// KArchiveFile ///////////////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveFilePrivate
{
public:
    KArchiveFilePrivate(qint64 _pos, qint64 _size)
        : pos(_pos)
        , size(_size)
    {
    }
    qint64 pos;
    qint64 size;
};

KArchiveFile::KArchiveFile(KArchive *t, const QString &name, int access, const QDateTime &date,
                           const QString &user, const QString &group,
                           const QString &symlink,
                           qint64 pos, qint64 size)
    : KArchiveEntry(t, name, access, date, user, group, symlink)
    , d(new KArchiveFilePrivate(pos, size))
{
}

KArchiveFile::~KArchiveFile()
{
    delete d;
}

qint64 KArchiveFile::position() const
{
    return d->pos;
}

qint64 KArchiveFile::size() const
{
    return d->size;
}

void KArchiveFile::setSize(qint64 s)
{
    d->size = s;
}

QByteArray KArchiveFile::data() const
{
    bool ok = archive()->device()->seek(d->pos);
    if (!ok) {
        //qCWarning(KArchiveLog) << "Failed to sync to" << d->pos << "to read" << name();
    }

    // Read content
    QByteArray arr;
    if (d->size) {
        arr = archive()->device()->read(d->size);
        Q_ASSERT(arr.size() == d->size);
    }
    return arr;
}

QIODevice *KArchiveFile::createDevice() const
{
    return new KLimitedIODevice(archive()->device(), d->pos, d->size);
}

bool KArchiveFile::isFile() const
{
    return true;
}

static QFileDevice::Permissions withExecutablePerms(
    QFileDevice::Permissions filePerms,
    mode_t perms)
{
    if (perms & 01)
      filePerms |= QFileDevice::ExeOther;

    if (perms & 010)
      filePerms |= QFileDevice::ExeGroup;

    if (perms & 0100)
      filePerms |= QFileDevice::ExeOwner;

    return filePerms;
}

namespace {
// Use a conservative, host-independent identity for archive names. Otherwise a
// plan checked on case-sensitive strings can become ambiguous on the output FS.
QString portablePathKey(const QString &path)
{
    return path.normalized(QString::NormalizationForm_C).toCaseFolded().normalized(QString::NormalizationForm_C);
}

bool safeComponent(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QChar(0))) {
        return false;
    }
#ifdef Q_OS_WIN
    if (name.contains(QLatin1Char(':')) || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        return false;
    }
    const auto base = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX")
        || base == QLatin1String("NUL") || base == QLatin1String("CONIN$") || base == QLatin1String("CONOUT$")) {
        return false;
    }
    if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT")))
        && (QStringLiteral("123456789").contains(base[3]) || base[3] == QChar(0x00b9)
            || base[3] == QChar(0x00b2) || base[3] == QChar(0x00b3))) {
        return false;
    }
#endif
    return true;
}

bool isLink(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

// The caller owns the output root. Reject static links below it; this is not
// a sandbox against another process concurrently replacing its directories.
bool safeExistingPath(const QString &root, const QString &path)
{
    const QFileInfo rootInfo(root);
    if (isLink(rootInfo) || (rootInfo.exists() && !rootInfo.isDir())) {
        return false;
    }
    const auto relative = QDir(root).relativeFilePath(path);
    if (relative == QLatin1String(".")) {
        return true;
    }
    QString current = root;
    const auto parts = relative.split(QLatin1Char('/'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (!safeComponent(parts[i])) {
            return false;
        }
        current += QLatin1Char('/') + parts[i];
        const QFileInfo info(current);
        if (isLink(info) || (info.exists() && !info.isFile() && !info.isDir())
            || (i + 1 < parts.size() && info.exists() && !info.isDir())) {
            return false;
        }
    }
    return true;
}

bool safeLinkTarget(const QString &root, const QString &link, const QMap<QString, QString> &links,
                    const QMap<QString, QString> &linkNames, QString &resolved)
{
    QStringList position = QDir(root).relativeFilePath(QFileInfo(link).absolutePath()).split(QLatin1Char('/'));
    if (position == QStringList{QStringLiteral(".")}) {
        position.clear();
    }
    QStringList pending = links.value(link).split(QLatin1Char('/'));
    int followed = 0;
    while (!pending.isEmpty()) {
        const auto component = pending.takeFirst();
        if (component.isEmpty() || component == QLatin1String(".")) {
            continue;
        }
        if (component == QLatin1String("..")) {
            if (position.isEmpty()) {
                return false;
            }
            position.removeLast();
            continue;
        }
        if (!safeComponent(component)) {
            return false;
        }
        const auto candidate = root + QLatin1Char('/') + (position + QStringList{component}).join(QLatin1Char('/'));
        if (!safeExistingPath(root, candidate)) {
            return false;
        }
        const auto plannedLink = linkNames.constFind(portablePathKey(candidate));
        if (plannedLink != linkNames.cend()) {
            // A differently spelled link is dangling on some filesystems and
            // followed on others. Reject it instead of guessing the FS rules.
            if (plannedLink.value() != candidate) {
                return false;
            }
            // Expand links before '..', matching filesystem semantics. A lexical
            // cleanPath would miss escapes such as pivot/../outside with pivot='.'.
            if (++followed > 40) {
                return false;
            }
            pending = links.value(candidate).split(QLatin1Char('/')) + pending;
        } else {
            position.append(component);
        }
    }
    resolved = position.isEmpty() ? root : root + QLatin1Char('/') + position.join(QLatin1Char('/'));
    return safeExistingPath(root, resolved);
}
}

bool KArchiveFile::copyTo(const QString &dest, const QAtomicInt& isCanceled) const
{
    const QFileInfo directory(dest);
    const QString path = dest + QLatin1Char('/') + name();
    const QFileInfo target(path);
    if (isCanceled.loadAcquire() != 0 || d->size < 0 || !safeComponent(name())
        || isLink(directory) || !directory.isDir() || isLink(target) || (target.exists() && !target.isFile())) {
        return false;
    }
    QSaveFile f(path);
    f.setDirectWriteFallback(false); // Never truncate an existing hard-link target.
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    std::unique_ptr<QIODevice> inputDev(createDevice());
    if (!inputDev) {
        return false;
    }
    const qint64 chunkSize = 1024 * 1024;
    qint64 remainingSize = d->size;
    QByteArray array(int(qMin(chunkSize, remainingSize)), 0);
    while (remainingSize > 0) {
        if (isCanceled.loadAcquire() != 0) {
            return false;
        }
        const qint64 currentChunkSize = qMin(chunkSize, remainingSize);
        const qint64 n = inputDev->read(array.data(), currentChunkSize);
        if (n <= 0 || n > currentChunkSize || f.write(array.constData(), n) != n) {
            return false;
        }
        remainingSize -= n;
    }
    if (isCanceled.loadAcquire() != 0 || !f.flush()) {
        return false;
    }
    f.setPermissions(withExecutablePerms(f.permissions(), permissions()));
    return f.commit();
}

////////////////////////////////////////////////////////////////////////
//////////////////////// KArchiveDirectory /////////////////////////////////
////////////////////////////////////////////////////////////////////////

KArchiveDirectory::KArchiveDirectory(KArchive *t, const QString &name, int access,
                                     const QDateTime &date,
                                     const QString &user, const QString &group,
                                     const QString &symlink)
    : KArchiveEntry(t, name, access, date, user, group, symlink)
    , d(new KArchiveDirectoryPrivate(this))
{
}

KArchiveDirectory::~KArchiveDirectory()
{
    delete d;
}

QStringList KArchiveDirectory::entries() const
{
    return d->entries.keys();
}

const KArchiveEntry *KArchiveDirectory::entry(const QString &_name) const
{
    KArchiveDirectory *dummy;
    return d->entry(_name, &dummy);
}

const KArchiveFile *KArchiveDirectory::file(const QString &name) const
{
    const KArchiveEntry *e = entry(name);
    if (e && e->isFile()) {
        return static_cast<const KArchiveFile *>(e);
    }
    return nullptr;
}

void KArchiveDirectory::addEntry(KArchiveEntry *entry)
{
    addEntryV2(entry);
}

bool KArchiveDirectory::addEntryV2(KArchiveEntry *entry)
{
    if (d->entries.value(entry->name())) {
        qCWarning(KArchiveLog) << "directory " << name()
                    << "has entry" << entry->name() << "already";
        delete entry;
        return false;
    }
    d->entries.insert(entry->name(), entry);
    return true;
}

void KArchiveDirectory::removeEntry(KArchiveEntry *entry)
{
    if (!entry) {
        return;
    }

    QHash<QString, KArchiveEntry *>::Iterator it = d->entries.find(entry->name());
    // nothing removed?
    if (it == d->entries.end()) {
        qCWarning(KArchiveLog) << "directory " << name()
                   << "has no entry with name " << entry->name();
        return;
    }
    if (it.value() != entry) {
        qCWarning(KArchiveLog) << "directory " << name()
                   << "has another entry for name " << entry->name();
        return;
    }
    d->entries.erase(it);
}

bool KArchiveDirectory::isDirectory() const
{
    return true;
}

static bool sortByPosition(const KArchiveFile *file1, const KArchiveFile *file2)
{
    return file1->position() < file2->position();
}

bool KArchiveDirectory::copyTo(const QString &dest, const QAtomicInt& isCanceled, bool recursiveCopy) const
{
    const QString destDir(QDir(dest).absolutePath());
    if (isCanceled.loadAcquire() != 0 || !safeExistingPath(destDir, destDir)) {
        return false;
    }
    QList<const KArchiveFile *> fileList;
    QMap<const KArchiveFile *, QString> fileToDir;
    QStringList directories;
    QMap<QString, QString> links;
    QMap<QString, QString> linkNames;
    QMap<QString, QString> linkOutputs;
    QSet<QString> outputs;
    qint64 totalSize = 0;
    QStack<const KArchiveDirectory *> dirStack;
    QStack<QString> dirNameStack;
    dirStack.push(this);
    dirNameStack.push(destDir);
    // Preflight the entire tree before creating anything or following links.
    while (!dirStack.isEmpty()) {
        if (isCanceled.loadAcquire() != 0) {
            return false;
        }
        const auto *curDir = dirStack.pop();
        const auto curDirName = dirNameStack.pop();
        if (!safeExistingPath(destDir, curDirName)
            || (QFileInfo::exists(curDirName) && !QFileInfo(curDirName).isDir())) {
            return false;
        }
        directories.append(curDirName);
        for (const auto &name : curDir->entries()) {
            if (isCanceled.loadAcquire() != 0 || !safeComponent(name)) {
                return false;
            }
            const auto *entry = curDir->entry(name);
            if (!entry) {
                return false;
            }
            const QString path = curDirName + QLatin1Char('/') + name;
            if (!safeExistingPath(destDir, path)) {
                return false;
            }
            QString output = path;
            if (!entry->symLinkTarget().isEmpty()) {
                const auto target = entry->symLinkTarget();
                if (!QDir::isRelativePath(target) || target.contains(QLatin1Char('\\')) || target.contains(QChar(0))) {
                    return false;
                }
#ifdef Q_OS_WIN
                if (!output.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
                    output += QLatin1String(".lnk");
                }
#endif
                if (!safeExistingPath(destDir, output) || QFileInfo::exists(output)) {
                    return false;
                }
                links.insert(path, target);
                linkNames.insert(portablePathKey(path), path);
                linkOutputs.insert(path, output);
            } else if (entry->isFile()) {
                const auto *file = static_cast<const KArchiveFile *>(entry);
                if (file->size() < 0 || file->size() > std::numeric_limits<qint64>::max() - totalSize
                    || (QFileInfo::exists(path) && !QFileInfo(path).isFile())) {
                    return false;
                }
                totalSize += file->size();
                fileList.append(file);
                fileToDir.insert(file, curDirName);
            } else if (entry->isDirectory()) {
                if (!recursiveCopy) {
                    continue;
                }
                dirStack.push(static_cast<const KArchiveDirectory *>(entry));
                dirNameStack.push(path);
            } else {
                return false;
            }
            output = portablePathKey(output);
            if (outputs.contains(output)) {
                return false;
            }
            outputs.insert(output);
        }
    }
    QMap<QString, QString> resolvedTargets;
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        QString resolved;
        if (isCanceled.loadAcquire() != 0 || !safeLinkTarget(destDir, it.key(), links, linkNames, resolved)) {
            return false;
        }
        resolvedTargets.insert(it.key(), resolved);
    }
    for (const auto &directory : directories) {
        if (isCanceled.loadAcquire() != 0 || !safeExistingPath(destDir, directory) || !QDir().mkpath(directory)) {
            return false;
        }
    }
    std::sort(fileList.begin(), fileList.end(), sortByPosition);
    for (const auto *file : fileList) {
        const auto directory = fileToDir.value(file);
        if (!safeExistingPath(destDir, directory) || !file->copyTo(directory, isCanceled)) {
            return false;
        }
    }
    // No ordinary file is ever written through a link created by this archive.
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        const auto output = linkOutputs.value(it.key());
        if (isCanceled.loadAcquire() != 0 || !safeExistingPath(destDir, QFileInfo(output).absolutePath())) {
            return false;
        }
#ifdef Q_OS_WIN
        QFile target(resolvedTargets.value(it.key())); // Windows shortcuts need a full target path.
#else
        QFile target(it.value());
#endif
        if (!target.link(output)) {
            return false;
        }
    }
    return isCanceled.loadAcquire() == 0;
}

void KArchive::virtual_hook(int, void *)
{
    /*BASE::virtual_hook( id, data )*/;
}

void KArchiveEntry::virtual_hook(int, void *)
{
    /*BASE::virtual_hook( id, data );*/
}

void KArchiveFile::virtual_hook(int id, void *data)
{
    KArchiveEntry::virtual_hook(id, data);
}

void KArchiveDirectory::virtual_hook(int id, void *data)
{
    KArchiveEntry::virtual_hook(id, data);
}
