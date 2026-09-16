#pragma once
#include "authenticode_p.h"
namespace wintrust_fixture {
struct State {
    LONG status=ERROR_SUCCESS;
    int verifies=0,closes=0;
    bool correctParameters=true,hasProvider=true,hasSigner=true;
    HANDLE expectedFile=INVALID_HANDLE_VALUE;
    std::wstring expectedPath;
    CRYPT_PROVIDER_DATA provider{};
    CRYPT_PROVIDER_SGNR signer{};
    CRYPT_PROVIDER_CERT chain{};
    CERT_CONTEXT certificate{};
    BYTE bytes[3]={'a','b','c'};
    State() {
        provider.cbStruct=sizeof(provider);
        signer.cbStruct=sizeof(signer); signer.csCertChain=1; signer.pasCertChain=&chain;
        chain.cbStruct=sizeof(chain); chain.pCert=&certificate;
        certificate.dwCertEncodingType=X509_ASN_ENCODING;
        certificate.pbCertEncoded=bytes; certificate.cbCertEncoded=3;
    }
};
inline State* active=nullptr;
inline LONG WINAPI verify(HWND window,GUID* action,LPVOID input) {
    auto& fixture=*active;
    const GUID expectedAction=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    auto& data=*static_cast<WINTRUST_DATA*>(input);
    fixture.correctParameters &= window==INVALID_HANDLE_VALUE && IsEqualGUID(*action,expectedAction)
        && data.cbStruct==sizeof(data) && data.dwUIChoice==WTD_UI_NONE
        && data.dwUnionChoice==WTD_CHOICE_FILE && data.fdwRevocationChecks==WTD_REVOKE_WHOLECHAIN
        && (data.dwProvFlags&WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)
        && !(data.dwProvFlags&WTD_REVOCATION_CHECK_NONE)
        && data.pFile && data.pFile->cbStruct==sizeof(WINTRUST_FILE_INFO)
        && data.pFile->hFile==fixture.expectedFile && data.pFile->pcwszFilePath==fixture.expectedPath;
    if(data.dwStateAction==WTD_STATEACTION_CLOSE) {
        ++fixture.closes;
        fixture.correctParameters &= data.hWVTStateData==&fixture;
        data.hWVTStateData=nullptr;
        return ERROR_SUCCESS;
    }
    fixture.correctParameters &= data.dwStateAction==WTD_STATEACTION_VERIFY;
    ++fixture.verifies;
    data.hWVTStateData=&fixture;
    return fixture.status;
}
inline CRYPT_PROVIDER_DATA* WINAPI provider(HANDLE state) {
    active->correctParameters &= state==active;
    return active->hasProvider?&active->provider:nullptr;
}
inline CRYPT_PROVIDER_SGNR* WINAPI signer(CRYPT_PROVIDER_DATA* data,DWORD index,BOOL counter,DWORD counterIndex) {
    active->correctParameters &= data==&active->provider && index==0 && counter==FALSE && counterIndex==0;
    return active->hasSigner?&active->signer:nullptr;
}
inline zzlogg::update::detail::WinTrustApi api() { return {verify,provider,signer}; }
inline zzlogg::update::detail::PublisherFingerprint abcFingerprint() {
    return {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
}
}
