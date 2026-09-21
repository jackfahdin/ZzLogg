#pragma once
// Transaction engine process contract, single source for the production
// engine entry (txengine_main.cpp) and every test harness that must agree
// with it: the TxOutcome -> process exit code mapping, the usage/protocol
// rejection codes, and the txid locator acceptance. The Inno Setup restricted
// entries and the coordinator consume these codes; never duplicate the
// mapping or the hex acceptance in a caller.
#include "txengine_win.h"
#include <cstdint>
#include <string>
namespace zzlogg::updater::detail {
// 0: applied/recovered/nothing-to-recover; 2: argv contract rejection;
// 41: bootstrap rejection (BootstrapRejected, bootstrap_win_p.h);
// 42-46: per terminal outcome below; 47: protocol violation;
// 48: Inno Setup restricted-entry runtime failure (never an engine exit code).
inline constexpr int UsageRejected=2;
inline constexpr int ProtocolViolation=47;
// Set by the Inno Setup restricted entries (ZzLogg.iss) before process exit when the
// failure is installer-side runtime work rather than a usage rejection or a
// target-recheck rejection: busy transaction, protected-root/staging creation
// failure, missing journal, recovery staging failure. The engine never exits
// with this code; the coordinator treats any nonzero installer exit as Failed.
inline constexpr int InstallerRuntimeFailure=48;
inline int exitForOutcome(TxOutcome outcome) {
    switch(outcome) {
    case TxOutcome::Applied: case TxOutcome::Recovered: case TxOutcome::NothingToRecover: return 0;
    case TxOutcome::Rejected: return 42;
    case TxOutcome::Conflict: return 43;
    case TxOutcome::RolledBack: return 44;
    case TxOutcome::NeedsAuthorizedRecovery: return 45;
    case TxOutcome::RecoveryFailed: return 46;
    default: return ProtocolViolation;
    }
}
// The txid locator acceptance: exactly 16 lowercase hexadecimal digits,
// nonzero. credentialLocator (bootstrap_win_p.h) generates matching locators.
inline bool parseTxid(const std::wstring& text,std::uint64_t& value) {
    value=0;
    if(text.size()!=16) return false;
    for(const auto c:text) {
        value<<=4;
        if(c>=L'0' && c<=L'9') value|=c-L'0';
        else if(c>=L'a' && c<=L'f') value|=c-L'a'+10;
        else return false;
    }
    return value!=0;
}
}
