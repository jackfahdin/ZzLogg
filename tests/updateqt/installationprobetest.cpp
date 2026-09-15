#include <QtTest>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QProcess>
#include <cstring>
#include "installationprobe_p.h"

using namespace zzlogg::updateqt;

namespace {
class FakeRegistry final : public InstallationRegistryReader {
public:
    InstallationRegistration value;
    InstallationRegistration readMachine64() const override { return value; }
};

bool writeFile(const QString& path, const QByteArray& contents)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size()
        && file.commit();
}

#ifdef Q_OS_WIN
QByteArray utf16(const QString& text, bool terminated = true)
{
    QByteArray bytes(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
    if (terminated) bytes.append(2, '\0');
    return bytes;
}

struct RawValue {
    DWORD type = REG_SZ;
    QByteArray bytes;
    LSTATUS sizeStatus = ERROR_SUCCESS;
    LSTATUS dataStatus = ERROR_SUCCESS;
    DWORD claimedSize = 0;
    bool changeType = false;
};

InstallationRegistration readRaw(const RawValue& location, const RawValue& schema)
{
    return readInstallationValues([&](const wchar_t* name, DWORD* type, BYTE* data,
                                      DWORD* size) -> LSTATUS {
        const auto& value = QString::fromWCharArray(name) == QStringLiteral("InstallLocation")
            ? location : schema;
        *type = data && value.changeType ? REG_BINARY : value.type;
        if (!data) {
            *size = value.claimedSize ? value.claimedSize : DWORD(value.bytes.size());
            return value.sizeStatus;
        }
        if (value.dataStatus != ERROR_SUCCESS) return value.dataStatus;
        if (*size < DWORD(value.bytes.size())) return ERROR_MORE_DATA;
        std::memcpy(data, value.bytes.constData(), size_t(value.bytes.size()));
        *size = DWORD(value.bytes.size());
        return ERROR_SUCCESS;
    });
}

// Remove only this link with the native nonrecursive API, before QTemporaryDir cleanup.
class ScopedLink {
public:
    QString path;
    bool directory = false;
    bool created = false;
    ~ScopedLink()
    {
        if (path.isEmpty()) return;
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const DWORD attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                qFatal("Cannot inspect isolated test link for safe cleanup");
            return;
        }
        if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
        const BOOL removed = directory ? RemoveDirectoryW(native.c_str()) : DeleteFileW(native.c_str());
        if (!removed) qFatal("Cannot remove isolated test link; refusing recursive cleanup");
    }
};
#endif
}

class InstallationProbeTest : public QObject {
    Q_OBJECT
private slots:
    // Catches missing probe logic, prefix matching, trusting copied markers and malformed files.
    void filesystemEvidence_data()
    {
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<InstallationKind>("expected");
        for (const char* row : {"registered", "case", "lf", "printable", "supplementary", "max-marker"})
            QTest::newRow(row) << QString::fromLatin1(row) << InstallationKind::Registered;
        QTest::newRow("legacy") << QStringLiteral("legacy") << InstallationKind::Legacy;
        QTest::newRow("portable") << QStringLiteral("portable") << InstallationKind::Unregistered;
        for (const char* row : {"copy", "prefix", "missing-exe", "missing-marker", "read-denied",
                 "orphan-marker", "unknown-schema", "empty-root", "relative-root", "nul-root",
                 "relative-exe", "nul-exe", "exe-directory", "marker-directory", "over-marker",
                 "empty-version", "no-newline", "multiline", "nul-marker", "control-marker",
                 "invalid-utf8", "wrong-prefix", "dot-segment", "missing-root", "empty-exe",
                 "wrong-exe-name", "unc-root", "device-root", "drive-relative", "root-relative",
                 "dot-root", "trailing-dot", "trailing-space", "alternate-stream", "wildcard-root",
                 "double-separator", "marker-locked"})
            QTest::newRow(row) << QString::fromLatin1(row) << InstallationKind::Invalid;
    }

    void filesystemEvidence()
    {
#if !defined(Q_OS_WIN) || !defined(_M_X64)
        QSKIP("Windows x64 filesystem probe required");
#else
        QFETCH(QString, scenario);
        QFETCH(InstallationKind, expected);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.filePath(QStringLiteral("install"));
        QVERIFY(QDir().mkpath(root));
        QString executable = root + QStringLiteral("/ZzLogg.exe");
        const QString marker = root + QStringLiteral("/.zzlogg-install-root");
        QByteArray text("ZzLogg 26.09.15\r\n");
        FakeRegistry registry;
        registry.value = {true, false, root, 1};
        if (scenario == "case") registry.value.location = root.toUpper();
        if (scenario == "legacy") registry.value.schema.reset();
        if (scenario == "unknown-schema") registry.value.schema = 2;
        if (scenario == "read-denied") registry.value.readFailed = true;
        if (scenario == "portable" || scenario == "orphan-marker") registry.value = {};
        if (scenario == "empty-root") registry.value.location.clear();
        if (scenario == "relative-root") registry.value.location = "install";
        if (scenario == "unc-root") registry.value.location = "//localhost/c$/install";
        if (scenario == "device-root") registry.value.location = "//?/" + root;
        if (scenario == "drive-relative") registry.value.location = "C:install";
        if (scenario == "root-relative") registry.value.location = "/install";
        if (scenario == "dot-root") registry.value.location = root + "/.";
        if (scenario == "trailing-dot") registry.value.location = root + ".";
        if (scenario == "trailing-space") registry.value.location = root + " ";
        if (scenario == "alternate-stream") registry.value.location = root + ":stream";
        if (scenario == "wildcard-root") registry.value.location = root + "*";
        if (scenario == "double-separator") registry.value.location = root + "//child";
        if (scenario == "nul-root") registry.value.location += QChar(0) + QStringLiteral("suffix");
        if (scenario == "missing-root") registry.value.location += "/missing";
        if (scenario == "copy" || scenario == "prefix") {
            registry.value.location = temporary.filePath(scenario == "copy" ? "original" : "inst");
            QVERIFY(QDir().mkpath(registry.value.location));
        }
        if (scenario == "lf") text = "ZzLogg build\n";
        if (scenario == "printable") text = QStringLiteral("ZzLogg dev build 中文+dirty\n").toUtf8();
        if (scenario == "supplementary") text = QByteArray::fromHex("5a7a4c6f676720f09f9a800a");
        if (scenario == "max-marker") text = "ZzLogg " + QByteArray(248, 'x') + "\n";
        if (scenario == "over-marker") text = "ZzLogg " + QByteArray(249, 'x') + "\n";
        if (scenario == "empty-version") text = "ZzLogg \n";
        if (scenario == "no-newline") text = "ZzLogg version";
        if (scenario == "multiline") text = "ZzLogg version\nother\n";
        if (scenario == "nul-marker") text = QByteArray("ZzLogg a\0b\n", 11);
        if (scenario == "control-marker") text = "ZzLogg a\tb\n";
        if (scenario == "invalid-utf8") text = QByteArray("ZzLogg \xff\n", 9);
        if (scenario == "wrong-prefix") text = "Other version\n";
        if (scenario == "exe-directory") QVERIFY(QDir().mkpath(executable));
        else if (scenario != "missing-exe") QVERIFY(writeFile(executable, "fixture"));
        if (scenario == "marker-directory") QVERIFY(QDir().mkpath(marker));
        else if (scenario != "missing-marker" && scenario != "portable") QVERIFY(writeFile(marker, text));
        if (scenario == "relative-exe") executable = "install/ZzLogg.exe";
        if (scenario == "empty-exe") executable.clear();
        if (scenario == "wrong-exe-name") {
            executable = root + "/Other.exe";
            QVERIFY(writeFile(executable, "fixture"));
        }
        if (scenario == "nul-exe") executable += QChar(0) + QStringLiteral("suffix");
        if (scenario == "dot-segment") executable = root + "/../install/ZzLogg.exe";
        HANDLE locked = INVALID_HANDLE_VALUE;
        if (scenario == "marker-locked") {
            locked = CreateFileW(QDir::toNativeSeparators(marker).toStdWString().c_str(), GENERIC_READ,
                0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            QVERIFY(locked != INVALID_HANDLE_VALUE);
        }
        const auto result = probeInstallation(executable, registry);
        if (locked != INVALID_HANDLE_VALUE) QVERIFY(CloseHandle(locked));
        QCOMPARE(result.kind, expected);
        if (expected == InstallationKind::Registered)
            QCOMPARE(result.installRoot, QFileInfo(root).canonicalFilePath());
        else QVERIFY(result.installRoot.isEmpty());
#endif
    }

    // Catches unbounded/NUL-terminated decoding, wrong registry types, and query races.
    void rawRegistry_data()
    {
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<bool>("valid");
        for (const char* row : {"sz", "unterminated", "expand", "legacy"})
            QTest::newRow(row) << QString::fromLatin1(row) << true;
        for (const char* row : {"empty", "relative", "embedded-nul", "odd-size", "oversize",
                 "wrong-location-type", "missing-location", "denied", "data-denied", "grow-race",
                 "type-race", "wrong-schema-type", "short-schema", "long-schema", "schema-denied",
                 "schema-data-denied", "invalid-surrogate"})
            QTest::newRow(row) << QString::fromLatin1(row) << false;
    }

    void rawRegistry()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows registry parser required");
#else
        QFETCH(QString, scenario);
        QFETCH(bool, valid);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        RawValue location{REG_SZ, utf16(temporary.path())};
        RawValue schema{REG_DWORD, QByteArray::fromHex("01000000")};
        if (scenario == "unterminated") location.bytes = utf16(temporary.path(), false);
        if (scenario == "expand") location = {REG_EXPAND_SZ, utf16("%SystemRoot%")};
        if (scenario == "legacy") schema.sizeStatus = ERROR_FILE_NOT_FOUND;
        if (scenario == "empty") location.bytes = utf16("");
        if (scenario == "relative") location.bytes = utf16("relative");
        if (scenario == "embedded-nul") location.bytes = utf16(temporary.path() + QChar(0) + "suffix");
        if (scenario == "odd-size") location.bytes.append('x');
        if (scenario == "oversize") location.claimedSize = 0xffffffff;
        if (scenario == "wrong-location-type") location.type = REG_BINARY;
        if (scenario == "missing-location") location.sizeStatus = ERROR_FILE_NOT_FOUND;
        if (scenario == "denied") location.sizeStatus = ERROR_ACCESS_DENIED;
        if (scenario == "data-denied") location.dataStatus = ERROR_ACCESS_DENIED;
        if (scenario == "grow-race") location.dataStatus = ERROR_MORE_DATA;
        if (scenario == "type-race") location.changeType = true;
        if (scenario == "wrong-schema-type") schema.type = REG_SZ;
        if (scenario == "short-schema") schema.bytes.chop(1);
        if (scenario == "long-schema") schema.bytes.append('x');
        if (scenario == "schema-denied") schema.sizeStatus = ERROR_ACCESS_DENIED;
        if (scenario == "schema-data-denied") schema.dataStatus = ERROR_FILE_NOT_FOUND;
        if (scenario == "invalid-surrogate") location.bytes = utf16(temporary.path() + QChar(0xd800));
        const auto result = readRaw(location, schema);
        QVERIFY(result.present);
        QCOMPARE(result.readFailed, !valid);
        if (valid) {
            QCOMPARE(result.schema, scenario == "legacy" ? std::optional<unsigned>{} : std::optional<unsigned>{1});
            QCOMPARE(result.location, scenario == "expand" ? qEnvironmentVariable("SystemRoot") : temporary.path());
        }
#endif
    }

    // Catches canonicalizing before inspecting links, and omitting any ancestor or file check.
    void rejectsLinks_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char* row : {"root", "ancestor", "registry-root", "executable", "marker"})
            QTest::newRow(row) << QString::fromLatin1(row);
    }

    void rejectsLinks()
    {
#if !defined(Q_OS_WIN) || !defined(_M_X64)
        QSKIP("Windows x64 link test required");
#else
        QFETCH(QString, scenario);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString target = temporary.filePath("target");
        QVERIFY(QDir().mkpath(target + "/install"));
        const QString root = scenario == "ancestor" ? target + "/install" : target;
        QVERIFY(writeFile(root + "/ZzLogg.exe", "fixture"));
        QVERIFY(writeFile(root + "/.zzlogg-install-root", "ZzLogg build\n"));
        ScopedLink link;
        link.directory = scenario == "root" || scenario == "ancestor" || scenario == "registry-root";
        link.path = temporary.filePath("link");
        QString executable = root + "/ZzLogg.exe";
        FakeRegistry registry;
        registry.value = {true, false, root, 1};
        if (link.directory) {
            QProcess process;
            process.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link.path),
                                      QDir::toNativeSeparators(target)});
            link.created = process.waitForFinished() && process.exitCode() == 0;
            if (!link.created) QSKIP("Environment does not permit creating a temporary junction");
            if (scenario == "registry-root") registry.value.location = link.path;
            else {
                executable = link.path + (scenario == "ancestor" ? "/install" : "") + "/ZzLogg.exe";
                registry.value.location = QFileInfo(executable).absolutePath();
            }
        } else {
            const QString name = scenario == "marker" ? "/.zzlogg-install-root" : "/ZzLogg.exe";
            const QString original = root + name;
            const QString stored = temporary.filePath("stored-file");
            QVERIFY(QFile::rename(original, stored));
            link.path = original;
            link.created = CreateSymbolicLinkW(QDir::toNativeSeparators(original).toStdWString().c_str(),
                QDir::toNativeSeparators(stored).toStdWString().c_str(), 0x2) != 0;
            if (!link.created) QSKIP("Environment does not permit creating a temporary file symlink");
        }
        const auto result = probeInstallation(executable, registry);
        QCOMPARE(result.kind, InstallationKind::Invalid);
        QVERIFY(result.installRoot.isEmpty());
#endif
    }

    // Catches case-folded text equality confusing two distinct NTFS directories.
    void distinguishesCaseSensitiveDirectories()
    {
#if !defined(Q_OS_WIN) || !defined(_M_X64)
        QSKIP("Windows x64 case-sensitive directory test required");
#else
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto native = QDir::toNativeSeparators(temporary.path()).toStdWString();
        HANDLE directory = CreateFileW(native.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ
            | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        QVERIFY(directory != INVALID_HANDLE_VALUE);
        FILE_CASE_SENSITIVE_INFO info{FILE_CS_FLAG_CASE_SENSITIVE_DIR};
        const bool enabled = SetFileInformationByHandle(directory, FileCaseSensitiveInfo,
                                                        &info, sizeof(info)) != 0;
        const DWORD error = enabled ? ERROR_SUCCESS : GetLastError();
        QVERIFY(CloseHandle(directory));
        if (!enabled) QSKIP(qPrintable(QStringLiteral("Temporary directory case flag unavailable: %1").arg(error)));
        const QString first = temporary.filePath("Install");
        const QString second = temporary.filePath("install");
        QVERIFY(CreateDirectoryW(QDir::toNativeSeparators(first).toStdWString().c_str(), nullptr));
        QVERIFY(CreateDirectoryW(QDir::toNativeSeparators(second).toStdWString().c_str(), nullptr));
        QVERIFY(writeFile(first + "/ZzLogg.exe", "fixture"));
        QVERIFY(writeFile(first + "/.zzlogg-install-root", "ZzLogg build\n"));
        FakeRegistry registry;
        registry.value = {true, false, second, 1};
        const auto result = probeInstallation(first + "/ZzLogg.exe", registry);
        QCOMPARE(result.kind, InstallationKind::Invalid);
        QVERIFY(result.installRoot.isEmpty());
#endif
    }
};

QTEST_GUILESS_MAIN(InstallationProbeTest)
#include "installationprobetest.moc"
