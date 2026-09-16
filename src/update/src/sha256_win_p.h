#pragma once
#include "stablepackage_p.h"
#include <bcrypt.h>
#include <array>
#include <vector>
namespace zzlogg::update::detail {
class Sha256 {
public:
    Sha256() {
        DWORD bytes=0,size=0;
        if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return;
        if(BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),
                             sizeof(size),&bytes,0)<0 || bytes!=sizeof(size)) return;
        object_.resize(size);
        if(BCryptCreateHash(algorithm_,&hash_,object_.data(),size,nullptr,0,0)<0) hash_=nullptr;
    }
    ~Sha256() {
        if(hash_) BCryptDestroyHash(hash_);
        if(algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0);
    }
    Sha256(const Sha256&)=delete;
    Sha256& operator=(const Sha256&)=delete;
    bool add(const BYTE* data,ULONG size) {
        return hash_ && BCryptHashData(hash_,const_cast<PUCHAR>(data),size,0)>=0;
    }
    bool finish(std::array<std::uint8_t,32>& result) {
        return hash_ && BCryptFinishHash(hash_,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
    BCRYPT_HASH_HANDLE hash_=nullptr;
    std::vector<BYTE> object_;
};
}
