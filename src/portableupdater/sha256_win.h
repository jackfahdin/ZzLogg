#pragma once
#include "transaction.h"
#include "processidentity_win_p.h"
#include <bcrypt.h>
#include <array>
#include <stdexcept>
namespace zzlogg::portable {
inline void hashRequire(bool value,const std::string& error) {if(!value) throw std::runtime_error(error);}
inline std::string hashFile(const fs::path& path) {
    zzlogg::updater::detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    hashRequire(bool(file),"Cannot read file for verification: " + path.u8string());
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    hashRequire(GetFileInformationByHandleEx(file.get(),FileAttributeTagInfo,&attributes,sizeof(attributes)) &&
        !(attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"Invalid file for verification");
    BCRYPT_HASH_HANDLE hash=nullptr;
    hashRequire(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,nullptr,0,nullptr,0,0)>=0,"SHA-256 unavailable");
    std::array<unsigned char,65536> buffer{}; DWORD got=0;
    bool ok=true;
    for(;;) {
        if(!ReadFile(file.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&got,nullptr)) {ok=false;break;}
        if(!got) break;
        if(BCryptHashData(hash,buffer.data(),got,0)<0) {ok=false;break;}
    }
    std::array<unsigned char,32> digest{};
    if(ok) ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    BCryptDestroyHash(hash); hashRequire(ok,"SHA-256 file verification failed");
    constexpr char hex[]="0123456789abcdef"; std::string result;
    for(auto value:digest) {result+=hex[value>>4];result+=hex[value&15];}
    return result;
}
}
