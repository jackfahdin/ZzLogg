#pragma once
#include "stablepackage_p.h"
#include <wintrust.h>
#include <softpub.h>
#include <array>
#include <vector>

namespace zzlogg::update::detail {
using PublisherFingerprint=std::array<std::uint8_t,32>;
using PublisherPolicy=std::vector<PublisherFingerprint>;
// Only the native API boundary is replaceable in algorithm tests. It cannot
// construct VerifiedPackage or change the production publisher policy.
struct WinTrustApi {
    decltype(&WinVerifyTrust) verify=nullptr;
    decltype(&WTHelperProvDataFromStateData) providerData=nullptr;
    decltype(&WTHelperGetProvSignerFromChain) signer=nullptr;
};
PackageVerificationError checkAuthenticode(HANDLE,const std::wstring&,const PublisherPolicy&,const WinTrustApi&);
PackageVerificationError verifyAuthenticode(HANDLE,const std::wstring&,const PublisherPolicy&);
}
