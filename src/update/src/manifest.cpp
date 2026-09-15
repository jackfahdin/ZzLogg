#include "envelope.h"
#include "strictjson.h"
#include "zzlogg/update/signature.h"
#include <algorithm>
#include <set>
namespace zzlogg::update::detail {
namespace {
bool validId(std::string_view id) {
    return !id.empty() && id.size()<=64 && std::all_of(id.begin(),id.end(),[](char c) {
        return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-';
    });
}
bool fixtureKey(const std::vector<std::uint8_t>& bytes) {
    constexpr char hex[]="0123456789abcdef";
    std::string value;
    for(const auto byte:bytes) { value+=hex[byte>>4]; value+=hex[byte&15]; }
    return value=="d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
        || value=="3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
}
}
EnvelopeResult verifyEnvelope(std::string_view input, const VerificationContext& context) {
    const auto json=parseStrictJson(input,256*1024);
    if(!json || !json->is_object() || json->size()!=4
       || !json->contains("schema") || !json->at("schema").is_number_integer()
       || json->at("schema")!=1
       || !json->contains("keyId") || !json->at("keyId").is_string()
       || !json->contains("payload") || !json->at("payload").is_string()
       || !json->contains("signature") || !json->at("signature").is_string())
        return {};
    const auto id=json->at("keyId").get<std::string>();
    if(!validId(id)) return {};
    if(context.keys.empty()
       || (context.environment!=TrustEnvironment::Production && context.environment!=TrustEnvironment::Test))
        return {{},VerificationError::TrustInvalid};
    std::set<std::string> ids;
    const TrustedKey* selected=nullptr;
    for(const auto& key:context.keys) {
        if(!validId(key.id) || !ids.insert(key.id).second || key.publicKey.size()!=32
           || (key.purpose!=KeyPurpose::Production && key.purpose!=KeyPurpose::Test)
           || (context.environment==TrustEnvironment::Production
               && (key.purpose!=KeyPurpose::Production || fixtureKey(key.publicKey))))
            return {{},VerificationError::TrustInvalid};
        if(key.id==id) selected=&key;
    }
    if(!selected) return {{},VerificationError::TrustInvalid};
    const auto payload=decodeBase64(json->at("payload").get_ref<const std::string&>(),128*1024);
    const auto signature=decodeBase64(json->at("signature").get_ref<const std::string&>(),64);
    if(!payload || !signature || signature->size()!=64) return {};
    const std::string message="ZzLogg update manifest v1\n"+id+"\n"+*payload;
    if(!verifyEd25519(message,selected->publicKey,{signature->begin(),signature->end()}))
        return {{},VerificationError::SignatureInvalid};
    return {*payload,VerificationError::None,*selected};
}
}
