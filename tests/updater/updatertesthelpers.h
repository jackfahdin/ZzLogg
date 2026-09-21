#pragma once
// Shared helpers for the updater test targets (fixture and harnesses): the
// current-user SID lookup and the 0-on-reject txid parse wrapper. Both are
// thin adapters over production contracts — parseTxid lives in
// txcontract_win_p.h — so test expectations can never drift from the engine.
#include "processidentity_win_p.h"
#include "txcontract_win_p.h"
#include <sddl.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace zzlogg::updater::detail {
// Temp directory in the exact form the leases compare against. GetTempPathW
// may hand back an 8.3 short name or a substituted drive; every lease checks
// GetFinalPathNameByHandleW against the requested path, so an unnormalised
// root fails closed on environments that never come up on a developer box.
inline std::wstring testTempDirectory() {
    std::array<wchar_t,MAX_PATH> raw{};
    if(!GetTempPathW(static_cast<DWORD>(raw.size()),raw.data())) return {};
    Handle directory(CreateFileW(raw.data(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    if(!directory) return raw.data();
    std::array<wchar_t,32768> resolved{};
    const auto length=GetFinalPathNameByHandleW(directory.get(),resolved.data(),
        static_cast<DWORD>(resolved.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!length || length>=resolved.size()) return raw.data();
    std::wstring value(resolved.data(),length);
    constexpr std::wstring_view prefix=L"\\\\?\\";
    if(value.compare(0,prefix.size(),prefix)==0) value.erase(0,prefix.size());
    return value;
}
// Fresh, independently named working directory for one harness run.
inline std::filesystem::path createTestRoot(const std::filesystem::path& base) {
    std::array<unsigned char,16> nonce{};
    if(!randomBytes(nonce.data(),static_cast<ULONG>(nonce.size())))
        throw std::runtime_error("cannot generate a unique coordinator test directory");
    std::wstring name=L"ZzLogg-coordinate-test-"+std::to_wstring(GetCurrentProcessId())+L"-";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto byte:nonce){name+=hex[byte>>4];name+=hex[byte&15];}
    auto root=base/name;
    // Atomic creation must succeed: never adopt or erase another run's files.
    if(!std::filesystem::create_directory(root))
        throw std::filesystem::filesystem_error("coordinator test directory already exists",root,
            std::make_error_code(std::errc::file_exists));
    return root;
}
// String SID of the current process user, empty on any lookup failure.
inline std::wstring currentUserSid() {
    HANDLE rawToken=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken)) return {};
    Handle token(rawToken);
    DWORD size=0; GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);
    if(!size || size>4096) return {};
    std::vector<BYTE> bytes(size);
    if(!GetTokenInformation(token.get(),TokenUser,bytes.data(),size,&size)) return {};
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)) return {};
    std::wstring result(sid); LocalFree(sid); return result;
}
// parseTxid with the test-side convention: the parsed value, 0 on rejection.
inline std::uint64_t parseHexId(const std::wstring& text) {
    std::uint64_t value=0;
    return parseTxid(text,value)?value:0;
}
}
