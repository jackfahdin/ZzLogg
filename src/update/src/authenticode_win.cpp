#include "authenticode_p.h"
namespace zzlogg::update::detail {
PackageVerificationError verifyAuthenticode(HANDLE file,const std::wstring& path,const PublisherPolicy& publishers) {
    if(publishers.empty()) return PackageVerificationError::PublisherPolicyMissing;
    struct Module {
        HMODULE value=LoadLibraryExW(L"wintrust.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        ~Module() { if(value) FreeLibrary(value); }
    } module;
    if(!module.value) return PackageVerificationError::SignatureUntrusted;
    WinTrustApi api;
    api.verify=reinterpret_cast<decltype(api.verify)>(GetProcAddress(module.value,"WinVerifyTrust"));
    api.providerData=reinterpret_cast<decltype(api.providerData)>(GetProcAddress(module.value,"WTHelperProvDataFromStateData"));
    api.signer=reinterpret_cast<decltype(api.signer)>(GetProcAddress(module.value,"WTHelperGetProvSignerFromChain"));
    return checkAuthenticode(file,path,publishers,api);
}
}
