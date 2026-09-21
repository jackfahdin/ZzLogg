#pragma once
#include "installlock_win.h"
#include "localchannel_win_p.h"
#include "runtimecopy_win_p.h"
#include <array>
#include <cstddef>
#include <memory>
namespace zzlogg::updater::detail {
inline constexpr int ExecutionDisabled=40;
inline constexpr int BootstrapRejected=41;
// The coordinator process's (ZzLoggUpdate.exe) own exit codes. The
// transaction engine and the Inno Setup restricted entries occupy 42-48
// (txcontract_win_p.h), so these start at 60 and never overlap.
// 0 = installed and restarted.
inline constexpr int PackageRejected=60;       // bootstrap bytes disagree with the package
inline constexpr int ElevationDeclined=61;     // the user refused the UAC prompt
inline constexpr int InstallerLaunchFailed=62; // the elevated launch itself failed
inline constexpr int RelayFailed=63;           // handshake, protocol or transaction failure
inline constexpr int RestartPending=64;        // installed but the application was not restarted
// Fixed-size, process-local anonymous mapping; only its numeric read handle is in argv.
inline constexpr std::size_t PathCapacity=240;
struct BootstrapData {
    uint32_t magic=0x42555a5a,version=4;
    uint64_t parentHandle=0;
    ProcessStamp parent{};
    TransactionId transaction{};
    SessionToken token{};
    // Reserved installation directory identity; valid only with DirectoryReserved.
    DirectoryIdentity directory{};
    uint32_t flags=0;
    // Optional restricted data directory for the post-transaction restart.
    // Empty means none; otherwise an absolute path, NUL-terminated within
    // capacity, free of control characters.
    wchar_t dataDirectory[PathCapacity]{};
    // Verified package and registered installation directory. Either the
    // whole group is present or the whole group is absent; never half a set.
    wchar_t installerPath[PathCapacity]{};
    wchar_t installRoot[PathCapacity]{};
    // Expected bytes from the signed manifest. The coordinator rechecks them
    // independently instead of trusting the parent's conclusion.
    uint64_t packageSize=0;
    uint8_t packageSha256[32]{};
};
// bootstrap carries a reserved directory identity: the child opens the
// computed existing mutex with SYNCHRONIZE and holds it (observer only) before
// Hello. Never waited on, never released, never installation authority.
inline constexpr uint32_t DirectoryReserved=1;
// GUI -> coordinator child launch request. Empty installer-chain fields
// degrade to a plain protocol handshake.
struct ChildLaunchRequest {
    TransactionId transaction{};
    SessionToken token{};
    const DirectoryIdentity* reservedIdentity=nullptr;
    std::wstring dataDirectory;
    std::wstring installerPath;
    std::wstring installRoot;
    uint64_t packageSize=0;
    std::array<uint8_t,32> packageSha256{};
};
bool launchCopy(const RuntimeCopy&,const ChildLaunchRequest&,ProcessIdentity&);
// An absolute local or UNC path without control characters. All three path
// fields share this rule.
bool absolutePathPlausible(const wchar_t* text,std::size_t length);
// Credential file: the coordinator -> installer/engine restricted bootstrap
// (design spec section 3). The restricted switch carries only the locator;
// the file lives in the current user's private temp under a random name with
// a current-user-only DACL. The engine validates ownership, shape, locator
// consistency and the live coordinator identity, then deletes the file.
struct CredentialData {
    uint32_t magic=0x43555a5a,version=1; // "ZZUC"
    uint64_t coordinatorPid=0,coordinatorCreated=0;
    TransactionId transaction{};
    SessionToken token{};
};
// 16 lowercase hex of the first 8 transaction bytes (big-endian), empty when
// they are all zero — byte-identical acceptance to the engine's parseTxid.
std::wstring credentialLocator(const TransactionId&);
// <temp>\ZzLoggTx-<locator>.cred; empty for a malformed locator.
std::wstring credentialPath(const std::wstring& locator);
// CREATE_NEW with the current-user-only DACL; never overwrites.
bool writeCredentialFile(const CredentialData&);
// Rejects missing/oversized files, foreign owners, shape violations,
// locator/transaction mismatch and dead or mismatched coordinator identity.
bool readCredentialFile(const std::wstring& locator,CredentialData&);
bool deleteCredentialFile(const std::wstring& locator);
class ChildBootstrap {
public:
    ChildBootstrap();~ChildBootstrap();
    bool open(int,wchar_t**);
    const BootstrapData& data() const { return data_; }
    const ProcessIdentity& parent() const { return parent_; }
    bool directoryReserved() const;
private:
    BootstrapData data_{};
    ProcessIdentity parent_;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
