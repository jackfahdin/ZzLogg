#include "zzlogg/update/signature.h"
#include <monocypher-ed25519.h>

namespace zzlogg::update {
bool verifyEd25519(std::string_view message,
                   const std::vector<std::uint8_t>& publicKey,
                   const std::vector<std::uint8_t>& signature)
{
    if (publicKey.size() != 32 || signature.size() != 64)
        return false;
    // An empty default string_view may have a null data pointer.
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(
        message.empty() ? "" : message.data());
    return crypto_ed25519_check(signature.data(), publicKey.data(), bytes,
                                message.size()) == 0;
}
}
