#pragma once
#include "txjournal_win.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zzlogg::updater {

// The exact 64-bit uninstall registration key. Every registry operation the
// engine performs targets this key and no other; the journal layer only
// enforces the HKLM\ prefix (txjournal_win.cpp), so the engine re-checks the
// precise key at its own boundary, including records replayed during recovery.
inline constexpr wchar_t kRegistrationKey[]=
    L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg";
bool allowedRegistryKey(const std::wstring& key);

// Registry boundary. Win32-style and injectable: production reads/writes the
// real 64-bit HKLM view; tests inject a fake. The engine passes the full key
// on every call so an implementation can prove nothing outside the
// whitelisted key was ever requested.
class TxRegistry {
public:
    virtual ~TxRegistry() = default;
    virtual bool readString(const std::wstring& key,const std::wstring& name,std::wstring& value)=0;
    virtual bool readDword(const std::wstring& key,const std::wstring& name,std::uint32_t& value)=0;
    virtual bool writeString(const std::wstring& key,const std::wstring& name,const std::wstring& value)=0;
};
// Real 64-bit HKLM view. Refuses any key other than kRegistrationKey.
std::unique_ptr<TxRegistry> productionRegistry();

// Machine-readable payload/file-set manifest, binary little-endian:
// header(16): magic(8) "ZZTXMAN1" | version(4)=1 | count(4); then per entry:
// pathBytes(4) | size(8) | sha256(32) | relative path (UTF-8, '/' separators,
// no terminator). Bounded: count <= kMaxManifestEntries, pathBytes in
// [1,kMaxManifestPathBytes], file <= kMaxManifestBytes. Relative paths reject
// empty/'/'-led components, ".."/".", backslashes, drive/absolute forms,
// control characters and trailing dots/spaces; entries must be unique under
// case-insensitive comparison. The manifest never lists itself.
inline constexpr std::size_t kMaxManifestEntries=4096,kMaxManifestPathBytes=384;
inline constexpr std::uint64_t kMaxManifestBytes=4ull<<20;
struct TxManifestEntry {
    std::wstring relpath; // normalized wide form of the UTF-8 relative path
    std::uint64_t size=0;
    std::array<std::uint8_t,32> sha256{};
};
enum class TxManifestError { None, Unreadable, BadFormat, TooLarge, UnsafePath };
TxManifestError parseManifestFile(const std::wstring& path,std::vector<TxManifestEntry>& entries);
// Payload-authoring side of the same format, shared with tests so the wire
// format has exactly one serializer.
bool writeManifestFile(const std::wstring& path,const std::vector<TxManifestEntry>& entries);

// SHA-256 of a local managed file, matching the update core's existing choice
// (bcrypt SHA-256). Refuses reparse points, directories and delete-sharing
// opens so the hashed bytes belong to the named file.
bool sha256FileContent(const std::wstring& path,std::array<std::uint8_t,32>& digest,
    std::uint64_t* size=nullptr);

enum class TxOutcome {
    Prepared,   // prepare() only: verification passed, nothing modified yet
    Applied,    // transaction applied and Complete journaled
    Rejected,   // verification/preflight failed before any modification
    Conflict,   // modified managed file or payload conflict; applied part already rolled back
    RolledBack, // ordinary failure; applied part already reversed
    Recovered,  // authorized recovery reversed the journaled remainder
    NothingToRecover, // journal complete or header-fresh: recovery is a zero operation
    // Journal torn/corrupt (including the zero-byte boundary where a crash
    // landed between journal.log CREATE_NEW and the header write): the scene
    // is preserved exactly as found and surfaced for explicit authorized
    // recovery; never silently treated as nothing-to-recover, never
    // auto-continued.
    NeedsAuthorizedRecovery,
    RecoveryFailed, // authorized recovery attempted but an undo step failed; scene preserved
};
struct TxEngineResult {
    TxOutcome outcome=TxOutcome::Rejected;
    std::wstring detail;
};

struct TxEngineRequest {
    std::wstring installRoot; // registered installation directory
    std::wstring stagingDir;  // protected staging: new file set + files.manifest
    std::wstring journalRoot; // protected transactions root (journal/backup live here)
    std::uint64_t txid=0;
    std::wstring displayVersion; // new DisplayVersion written to the uninstall key
};

// Production image-location assertion: the engine executable must live under
// the protected transaction root, and that root's ACL must grant write access
// to Administrators/SYSTEM only. Exposed so tests can exercise the failure
// paths against unprivileged temporary directories.
bool productionProtectedImage(const std::wstring& image,const std::wstring& protectedRoot);
using TxProtectedImageCheck=std::function<bool(const std::wstring& image,const std::wstring& root)>;

struct TxEngineOptions {
    TxJournalOptions journal;
    TxRegistry* registry=nullptr;       // nullptr selects productionRegistry()
    TxProtectedImageCheck protectedImage; // empty selects productionProtectedImage
    std::wstring selfImage;             // empty queries the current module
    // Injectable volume-space preflight, same contract as checkVolumeSpace;
    // empty selects checkVolumeSpace. The engine preflights every involved
    // volume: the journal volume (staging + backup) and the installation
    // volume (incoming payload bytes), merging the requirements into a single
    // call when both roots share a volume.
    std::function<TxVolumeCheck(const std::wstring& root,std::uint64_t stagingBytes,
        std::uint64_t backupBytes)> volumeCheck;
};

// Differential file-set + registration transaction engine. prepare() is
// strictly read-only (image location, registration, marker, manifests,
// staging hashes, volume space) and must succeed before the caller reports
// AwaitingAppExit; execute() performs every modification journal-first and
// only after the protocol Proceed gate. Ordinary failures reverse this run's
// changes in reverse order. recover() is the authorized recovery path: it
// rechecks the target, then replays the journal strictly in reverse,
// idempotently; it never accepts an external file list.
class TxEngine {
public:
    TxEngine(TxEngineRequest request,TxEngineOptions options);
    ~TxEngine();
    TxEngine(TxEngine&&) noexcept;
    TxEngine& operator=(TxEngine&&) noexcept;
    TxEngine(const TxEngine&)=delete;
    TxEngine& operator=(const TxEngine&)=delete;
    TxEngineResult prepare();
    TxEngineResult execute();
    static TxEngineResult recover(const std::wstring& journalRoot,std::uint64_t txid,
        const std::wstring& installRoot,TxEngineOptions options);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
