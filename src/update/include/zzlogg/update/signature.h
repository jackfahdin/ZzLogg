#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace zzlogg::update {
bool verifyEd25519(std::string_view message,
                   const std::vector<std::uint8_t>& publicKey,
                   const std::vector<std::uint8_t>& signature);
}
