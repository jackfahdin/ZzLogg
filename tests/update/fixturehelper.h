#pragma once
#include "zzlogg/update/manifest.h"
#include "strictjson.h"
#include <monocypher-ed25519.h>
#include <array>
#include <string>

namespace update_fixture {
inline nlohmann::json payload() {
    return {{"schema",1},{"product","com.gitcode.jackfahdinqt.zzlogg"},
        {"metadataSequence","2"},{"issuedAt",1799999900},{"expiresAt",1800003600},
        {"channel","stable"},{"releaseSequence","2"},{"version","26.10.00"},
        {"minUpdaterProtocol",1},{"minDataSchema",1},{"maxDataSchema",1},
        {"notes",{{"en","Test update"},{"zh_CN","测试更新"},{"zh_TW","測試更新"}}},
        {"artifacts",nlohmann::json::array({{
            {"os","windows"},{"arch","x64"},{"distribution","portable"},{"format","zip"},
            {"minOsVersion","10.0.19041"},{"url","https://updates.example.invalid/ZzLogg.zip"},
            {"size","1024"},{"sha256",std::string(64,'0')}
        }})}};
}
// Public RFC 8032 test 1 seed. Never a production secret.
inline std::array<std::uint8_t,64> secretKey() {
    std::array<std::uint8_t,32> seed{
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
    std::array<std::uint8_t,64> secret{};
    std::array<std::uint8_t,32> publicKey{};
    crypto_ed25519_key_pair(secret.data(),publicKey.data(),seed.data());
    return secret;
}
inline zzlogg::update::VerificationContext context() {
    const auto secret=secretKey();
    zzlogg::update::VerificationContext result;
    result.environment=zzlogg::update::TrustEnvironment::Test;
    result.keys.push_back({"fixture",{secret.begin()+32,secret.end()},zzlogg::update::KeyPurpose::Test});
    result.allowedHosts={"updates.example.invalid"};
    result.now=1800000000;
    result.buildTime=1799000000;
    return result;
}
inline std::string envelope(std::string_view payload, std::string id="fixture",
                            std::string domain="ZzLogg update manifest v1\n") {
    const auto secret=secretKey();
    std::string message=domain+id+"\n"+std::string(payload);
    std::array<std::uint8_t,64> signature{};
    crypto_ed25519_sign(signature.data(),secret.data(),
        reinterpret_cast<const std::uint8_t*>(message.data()),message.size());
    return nlohmann::json{{"schema",1},{"keyId",id},
        {"payload",zzlogg::update::detail::encodeBase64(payload)},
        {"signature",zzlogg::update::detail::encodeBase64(
            {reinterpret_cast<const char*>(signature.data()),signature.size()})}}.dump();
}
}
