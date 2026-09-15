#pragma once
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
namespace zzlogg::update::detail {
std::optional<nlohmann::json> parseStrictJson(std::string_view input, size_t limit);
std::optional<std::string> decodeBase64(std::string_view input, size_t limit);
std::string encodeBase64(std::string_view input);
}
