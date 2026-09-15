#include "zzlogg/update/manifest.h"
#include "zzlogg/update/urlpolicy.h"
#include "envelope.h"
#include "strictjson.h"
#include <monocypher-ed25519.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <tuple>
namespace zzlogg::update {
namespace {
using Json=nlohmann::json;
struct InvalidPayload {};
void require(bool valid) { if(!valid) throw InvalidPayload{}; }
void fields(const Json& value, std::initializer_list<const char*> names) {
    require(value.is_object() && value.size()==names.size());
    for(const auto* name:names) require(value.contains(name));
}
std::string string(const Json& value) {
    require(value.is_string());
    return value.get<std::string>();
}
std::uint64_t decimal(std::string_view text, std::uint64_t maximum, bool positive=true) {
    require(!text.empty() && text.size()<=20);
    require(text.size()==1 || text.front()!='0');
    require(std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';}));
    std::uint64_t value=0;
    const auto converted=std::from_chars(text.data(),text.data()+text.size(),value);
    require(converted.ec==std::errc{} && converted.ptr==text.data()+text.size());
    require(value<=maximum && (!positive || value!=0));
    return value;
}
std::uint64_t integer(const Json& value,std::uint64_t maximum) {
    require(value.is_number_integer());
    if(value.is_number_unsigned()) {
        const auto number=value.get<std::uint64_t>(); require(number<=maximum); return number;
    }
    const auto number=value.get<std::int64_t>();
    require(number>=0 && static_cast<std::uint64_t>(number)<=maximum);
    return static_cast<std::uint64_t>(number);
}
OsVersion osVersion(std::string_view text) {
    const auto first=text.find('.'), last=text.rfind('.');
    require(first!=text.npos && last!=first);
    constexpr auto max=std::numeric_limits<std::uint32_t>::max();
    return {static_cast<std::uint32_t>(decimal(text.substr(0,first),max,false)),
        static_cast<std::uint32_t>(decimal(text.substr(first+1,last-first-1),max,false)),
        static_cast<std::uint32_t>(decimal(text.substr(last+1),max,false))};
}
bool token(std::string_view text) {
    return !text.empty() && text.size()<=64 && std::all_of(text.begin(),text.end(),[](char c){
        return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_';
    });
}
Manifest parsePayload(const Json& json,const VerificationContext& context) {
    fields(json,{"schema","product","metadataSequence","issuedAt","expiresAt","channel",
        "releaseSequence","version","minUpdaterProtocol","minDataSchema","maxDataSchema",
        "notes","artifacts"});
    require(integer(json.at("schema"),1)==1);
    require(string(json.at("product"))=="com.gitcode.jackfahdinqt.zzlogg");
    Manifest result;
    result.metadataSequence=decimal(string(json.at("metadataSequence")),UINT64_MAX);
    result.releaseSequence=decimal(string(json.at("releaseSequence")),UINT64_MAX);
    constexpr std::uint64_t maxTimestamp=9007199254740991ULL;
    result.issuedAt=static_cast<std::int64_t>(integer(json.at("issuedAt"),maxTimestamp));
    result.expiresAt=static_cast<std::int64_t>(integer(json.at("expiresAt"),maxTimestamp));
    result.channel=string(json.at("channel"));
    require(result.channel=="stable" || result.channel=="preview");
    const auto version=parseVersion(string(json.at("version")));
    require(version.has_value()); result.version=*version;
    result.minUpdaterProtocol=static_cast<std::uint32_t>(integer(json.at("minUpdaterProtocol"),UINT32_MAX));
    require(result.minUpdaterProtocol>0);
    result.minDataSchema=static_cast<std::uint32_t>(integer(json.at("minDataSchema"),UINT32_MAX));
    result.maxDataSchema=static_cast<std::uint32_t>(integer(json.at("maxDataSchema"),UINT32_MAX));
    require(result.minDataSchema<=result.maxDataSchema);
    const auto& notes=json.at("notes"); fields(notes,{"en","zh_CN","zh_TW"});
    size_t index=0;
    for(const auto* language:{"en","zh_CN","zh_TW"}) {
        auto text=string(notes.at(language)); require(text.size()<=16*1024);
        result.notes[index++]=std::move(text);
    }
    const auto& artifacts=json.at("artifacts");
    require(artifacts.is_array() && !artifacts.empty() && artifacts.size()<=16);
    std::set<std::tuple<std::string,std::string,Distribution>> identities;
    for(const auto& item:artifacts) {
        fields(item,{"os","arch","distribution","format","minOsVersion","url","size","sha256"});
        Artifact artifact;
        artifact.os=string(item.at("os")); artifact.arch=string(item.at("arch"));
        require(token(artifact.os) && token(artifact.arch));
        const auto distribution=string(item.at("distribution"));
        require(distribution=="portable" || distribution=="installer");
        artifact.distribution=distribution=="portable" ? Distribution::Portable : Distribution::Installer;
        artifact.format=string(item.at("format"));
        require(artifact.format==(artifact.distribution==Distribution::Portable ? "zip" : "nsis-exe"));
        artifact.minOsVersion=osVersion(string(item.at("minOsVersion")));
        artifact.url=string(item.at("url")); require(isAllowedUpdateUrl(artifact.url,context.allowedHosts));
        artifact.size=decimal(string(item.at("size")),512ULL*1024*1024);
        artifact.sha256=string(item.at("sha256"));
        require(artifact.sha256.size()==64 && std::all_of(artifact.sha256.begin(),artifact.sha256.end(),
            [](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');}));
        require(identities.emplace(artifact.os,artifact.arch,artifact.distribution).second);
        result.artifacts.push_back(std::move(artifact));
    }
    return result;
}
}
VerificationResult verifyManifest(std::string_view input, const VerificationContext& context) {
    const auto envelope=detail::verifyEnvelope(input,context);
    if(!envelope.payload) return {{},envelope.error};
    const auto json=detail::parseStrictJson(*envelope.payload,128*1024);
    if(!json) return {};
    Manifest manifest;
    try { manifest=parsePayload(*json,context); }
    catch(const InvalidPayload&) { return {}; }
    catch(const nlohmann::json::exception&) { return {}; }
    if(context.now<0 || context.buildTime<0
       || (context.buildTime>context.now && context.buildTime-context.now>86400)
       || (manifest.issuedAt>context.now && manifest.issuedAt-context.now>300))
        return {{},VerificationError::ClockInvalid};
    if(manifest.expiresAt<=manifest.issuedAt
       || manifest.expiresAt-manifest.issuedAt>30*86400)
        return {{},VerificationError::PayloadInvalid};
    if(manifest.expiresAt<=context.now) return {{},VerificationError::Expired};
    if(manifest.channel!=context.channel) return {{},VerificationError::ChannelMismatch};
    AcceptedMetadata accepted; accepted.sequence=manifest.metadataSequence;
    crypto_sha512(accepted.payloadDigest.data(),
        reinterpret_cast<const std::uint8_t*>(envelope.payload->data()),envelope.payload->size());
    if(context.lastAccepted) {
        if(accepted.sequence<context.lastAccepted->sequence)
            return {{},VerificationError::Replay};
        if(accepted.sequence==context.lastAccepted->sequence
           && accepted.payloadDigest!=context.lastAccepted->payloadDigest)
            return {{},VerificationError::MetadataConflict};
    }
    return {VerifiedManifest(std::move(manifest),accepted,context.environment,*envelope.signingKey),
        VerificationError::None};
}
}
