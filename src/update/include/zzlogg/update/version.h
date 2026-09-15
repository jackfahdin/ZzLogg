#pragma once
#include <optional>
#include <string_view>

namespace zzlogg::update {
struct Version { unsigned year, month, patch; };
std::optional<Version> parseVersion(std::string_view text);
int compareVersion(Version a, Version b);
}
