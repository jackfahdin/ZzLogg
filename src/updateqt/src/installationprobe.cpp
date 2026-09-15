#include "installationprobe_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <cstring>
#include <vector>

namespace zzlogg::updateqt {
#ifdef Q_OS_WIN
namespace {
constexpr DWORD maximumRegistryBytes = 64 * 1024;

bool supportedAbsolutePath(const QString& path)
{
    // Deliberately support ordinary local drive paths only. Reject ambiguous Win32
    // spellings before normalization could erase a link-bearing component.
    QString value = path;
    value.replace('\\', '/');
    if (value.size() < 3 || !((value[0] >= 'A' && value[0] <= 'Z')
                             || (value[0] >= 'a' && value[0] <= 'z'))
        || value[1] != ':' || value[2] != '/' || !value.isValidUtf16()) return false;
    for (const QChar character : value)
        if (character.isNull() || character.category() == QChar::Other_Control) return false;
    if (value.mid(2).contains(':')) return false;
    const auto components = value.mid(3).split('/', Qt::KeepEmptyParts);
    for (qsizetype i = 0; i < components.size(); ++i) {
        const auto& component = components[i];
        if (component.isEmpty() && i == components.size() - 1) continue;
        if (component.isEmpty() || component == "." || component == ".."
            || component.endsWith('.') || component.endsWith(' ')
            || component.contains('*') || component.contains('?')
            || component.contains('<') || component.contains('>')
            || component.contains('|') || component.contains('"')) return false;
    }
    return true;
}

struct QueriedValue {
    LSTATUS status = ERROR_INVALID_DATA;
    DWORD type = 0;
    QByteArray bytes;
};

QueriedValue queryValue(const InstallationValueQuery& query, const wchar_t* name)
{
    QueriedValue value;
    DWORD size = 0;
    value.status = query(name, &value.type, nullptr, &size);
    if (value.status != ERROR_SUCCESS) return value;
    if (size == 0 || size > maximumRegistryBytes) {
        value.status = ERROR_INVALID_DATA;
        return value;
    }
    value.bytes.resize(size);
    DWORD actualType = 0;
    DWORD actualSize = size;
    value.status = query(name, &actualType, reinterpret_cast<BYTE*>(value.bytes.data()), &actualSize);
    if (value.status != ERROR_SUCCESS || actualType != value.type || actualSize != size) {
        // A value changing between the size and data queries is not stable evidence.
        value.status = ERROR_INVALID_DATA;
        value.bytes.clear();
    }
    return value;
}

std::optional<QString> locationFromValue(const QueriedValue& value)
{
    if (value.status != ERROR_SUCCESS || (value.type != REG_SZ && value.type != REG_EXPAND_SZ)
        || value.bytes.size() % sizeof(wchar_t) != 0) return std::nullopt;
    // RegQueryValueExW need not return a terminator. Copy into aligned storage and
    // decode the explicit bounded length; accept at most one optional trailing NUL.
    std::vector<wchar_t> units(size_t(value.bytes.size()) / sizeof(wchar_t));
    std::memcpy(units.data(), value.bytes.constData(), size_t(value.bytes.size()));
    if (!units.empty() && units.back() == 0) units.pop_back();
    if (units.empty()) return std::nullopt;
    QString path = QString::fromWCharArray(units.data(), qsizetype(units.size()));
    if (path.contains(QChar(0)) || !path.isValidUtf16()) return std::nullopt;
    if (value.type == REG_EXPAND_SZ) {
        const auto native = path.toStdWString();
        const DWORD required = ExpandEnvironmentStringsW(native.c_str(), nullptr, 0);
        if (required == 0 || required > maximumRegistryBytes / sizeof(wchar_t)) return std::nullopt;
        std::vector<wchar_t> expanded(required);
        const DWORD written = ExpandEnvironmentStringsW(native.c_str(), expanded.data(), required);
        if (written != required || expanded.back() != 0) return std::nullopt;
        path = QString::fromWCharArray(expanded.data(), required - 1);
    }
    if (!supportedAbsolutePath(path)) return std::nullopt;
    return path;
}

class RegistryKey {
public:
    HKEY value = nullptr;
    RegistryKey() = default;
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};

class WindowsRegistryReader final : public InstallationRegistryReader {
public:
    InstallationRegistration readMachine64() const override
    {
        RegistryKey key;
        const LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg", 0,
            KEY_READ | KEY_WOW64_64KEY, &key.value);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return {};
        if (status != ERROR_SUCCESS) return {false, true, {}, {}};
        return readInstallationValues([&](const wchar_t* name, DWORD* type, BYTE* data, DWORD* size) {
            return RegQueryValueExW(key.value, name, nullptr, type, data, size);
        });
    }
};

enum class PathState { Missing, Directory, File, Unsafe };

PathState inspectPath(const QString& path)
{
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? PathState::Missing : PathState::Unsafe;
    }
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) return PathState::Unsafe;
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? PathState::Directory : PathState::File;
}

bool safeDirectoryChain(const QString& path)
{
    if (!supportedAbsolutePath(path)) return false;
    const QString value = QDir::fromNativeSeparators(path);
    QString prefix = value.left(3);
    if (inspectPath(prefix) != PathState::Directory) return false;
    for (const auto& component : value.mid(3).split('/', Qt::SkipEmptyParts)) {
        if (!prefix.endsWith('/')) prefix += '/';
        prefix += component;
        if (inspectPath(prefix) != PathState::Directory) return false;
    }
    return true;
}

std::optional<BY_HANDLE_FILE_INFORMATION> directoryIdentity(const QString& path)
{
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    struct DirectoryHandle {
        HANDLE value;
        ~DirectoryHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } handle{CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info)
        || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return std::nullopt;
    return info;
}

bool validMarker(const QByteArray& bytes)
{
    if (bytes.size() > 256 || !bytes.startsWith("ZzLogg ") || !bytes.endsWith('\n')) return false;
    QByteArray version = bytes.mid(7);
    version.chop(1);
    if (version.endsWith('\r')) version.chop(1);
    if (version.isEmpty()) return false;
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(version);
    if (decoder.hasError()) return false;
    for (const auto character : text.toUcs4())
        if (!QChar::isPrint(character)) return false;
    return true;
}
} // namespace

InstallationRegistration readInstallationValues(const InstallationValueQuery& query)
{
    InstallationRegistration result{true, true, {}, {}};
    const auto location = locationFromValue(queryValue(query, L"InstallLocation"));
    if (!location) return result;
    const auto schema = queryValue(query, L"UpdateIdentitySchema");
    if (schema.status != ERROR_FILE_NOT_FOUND) {
        if (schema.status != ERROR_SUCCESS || schema.type != REG_DWORD
            || schema.bytes.size() != sizeof(DWORD)) return result;
        DWORD number = 0;
        std::memcpy(&number, schema.bytes.constData(), sizeof(number));
        result.schema = number;
    }
    result.location = *location;
    result.readFailed = false;
    return result;
}
#endif

InstallationIdentity probeInstallation(const QString& executablePath,
                                      const InstallationRegistryReader& registry)
{
#if defined(Q_OS_WIN) && (defined(_M_X64) || defined(__x86_64__))
    InstallationEvidence evidence;
    evidence.supportedPlatform = true;
    const auto registration = registry.readMachine64();
    evidence.registrationPresent = registration.present;
    evidence.readFailed = registration.readFailed;
    evidence.identitySchema = registration.schema;
    if (evidence.readFailed) return evaluateInstallation(evidence);
    if (!supportedAbsolutePath(executablePath)) return {};
    const QFileInfo executable(executablePath);
    const QString currentRoot = executable.absolutePath();
    if (executable.fileName().compare(QStringLiteral("ZzLogg.exe"), Qt::CaseInsensitive) != 0
        || !safeDirectoryChain(currentRoot) || inspectPath(executablePath) != PathState::File) return {};
    const QString markerPath = QDir(currentRoot).filePath(QStringLiteral(".zzlogg-install-root"));
    const auto markerState = inspectPath(markerPath);
    if (markerState == PathState::Unsafe || markerState == PathState::Directory) return {};
    evidence.markerPresent = markerState == PathState::File;
    if (evidence.markerPresent) {
        QFile marker(markerPath);
        if (!marker.open(QIODevice::ReadOnly)) return {};
        const auto bytes = marker.read(257);
        if (marker.error() != QFileDevice::NoError) return {};
        evidence.markerValid = validMarker(bytes);
    }
    if (!registration.present) return evaluateInstallation(evidence);
    if (!safeDirectoryChain(registration.location)) return {};
    // Inspect every component before Qt's native canonicalization follows paths.
    // These are observations, not a lock or an authorization for later file changes.
    const QString currentCanonical = QFileInfo(currentRoot).canonicalFilePath();
    const QString registeredCanonical = QFileInfo(registration.location).canonicalFilePath();
    if (currentCanonical.isEmpty() || registeredCanonical.isEmpty()) return {};
    const auto currentDirectory = directoryIdentity(currentRoot);
    const auto registeredDirectory = directoryIdentity(registration.location);
    if (!currentDirectory || !registeredDirectory) return {};
    evidence.safePaths = true;
    evidence.sameDirectory = currentCanonical.compare(registeredCanonical, Qt::CaseInsensitive) == 0
        && currentDirectory->dwVolumeSerialNumber == registeredDirectory->dwVolumeSerialNumber
        && currentDirectory->nFileIndexHigh == registeredDirectory->nFileIndexHigh
        && currentDirectory->nFileIndexLow == registeredDirectory->nFileIndexLow;
    evidence.installRoot = currentCanonical;
    return evaluateInstallation(evidence);
#else
    Q_UNUSED(executablePath);
    Q_UNUSED(registry);
    return {InstallationKind::Unsupported, {}};
#endif
}

InstallationIdentity probeCurrentInstallation()
{
#if defined(Q_OS_WIN) && (defined(_M_X64) || defined(__x86_64__))
    return probeInstallation(QCoreApplication::applicationFilePath(), WindowsRegistryReader{});
#else
    return {InstallationKind::Unsupported, {}};
#endif
}
} // namespace zzlogg::updateqt
