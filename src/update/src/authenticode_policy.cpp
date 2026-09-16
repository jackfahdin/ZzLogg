#include "authenticode_p.h"
#include "sha256_win_p.h"
#include <algorithm>
namespace zzlogg::update::detail {
PackageVerificationError checkAuthenticode(HANDLE handle,const std::wstring& path,
    const PublisherPolicy& publishers,const WinTrustApi& api) {
    if(publishers.empty()) return PackageVerificationError::PublisherPolicyMissing;
    if(!api.verify || !api.providerData || !api.signer) return PackageVerificationError::SignatureUntrusted;
    WINTRUST_FILE_INFO file{};
    file.cbStruct=sizeof(file); file.pcwszFilePath=path.c_str(); file.hFile=handle;
    WINTRUST_DATA data{};
    data.cbStruct=sizeof(data);
    data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice=WTD_CHOICE_FILE;
    data.pFile=&file;
    data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|WTD_DISABLE_MD2_MD4;
    data.dwUIContext=WTD_UICONTEXT_EXECUTE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    struct CloseState {
        const WinTrustApi& api; GUID& action; WINTRUST_DATA& data;
        ~CloseState() {
            data.dwStateAction=WTD_STATEACTION_CLOSE;
            api.verify(static_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data);
        }
    } close{api,action,data};
    // WinVerifyTrust returns LONG, not HRESULT: positive warnings also fail.
    if(api.verify(static_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data)!=0 || !data.hWVTStateData)
        return PackageVerificationError::SignatureUntrusted;
    auto* provider=api.providerData(data.hWVTStateData);
    if(!provider) return PackageVerificationError::SignatureUntrusted;
    // Index zero, explicitly not a countersigner (timestamp signer).
    auto* signer=api.signer(provider,0,FALSE,0);
    if(!signer || signer->dwError || !signer->csCertChain || !signer->pasCertChain)
        return PackageVerificationError::SignatureUntrusted;
    const auto& leaf=signer->pasCertChain[0];
    if(leaf.dwError || !leaf.pCert || !leaf.pCert->pbCertEncoded || !leaf.pCert->cbCertEncoded)
        return PackageVerificationError::SignatureUntrusted;
    Sha256 hash;
    PublisherFingerprint digest{};
    if(!hash.add(leaf.pCert->pbCertEncoded,leaf.pCert->cbCertEncoded) || !hash.finish(digest))
        return PackageVerificationError::SignatureUntrusted;
    return std::find(publishers.begin(),publishers.end(),digest)!=publishers.end()
        ? PackageVerificationError::None:PackageVerificationError::PublisherMismatch;
}
}
