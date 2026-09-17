#pragma once
// Shared helpers for the updater test targets (fixture and harnesses): the
// current-user SID lookup and the 0-on-reject txid parse wrapper. Both are
// thin adapters over production contracts — parseTxid lives in
// txcontract_win_p.h — so test expectations can never drift from the engine.
#include "processidentity_win_p.h"
#include "txcontract_win_p.h"
#include <sddl.h>
#include <cstdint>
#include <string>
#include <vector>
namespace zzlogg::updater::detail {
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
