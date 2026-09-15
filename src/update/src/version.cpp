#include "zzlogg/update/version.h"
#include <tuple>
namespace zzlogg::update {
std::optional<Version> parseVersion(std::string_view text)
{
    if (text.size() != 8 || text[2] != '.' || text[5] != '.') return std::nullopt;
    for (const auto i : {0, 1, 3, 4, 6, 7})
        if (text[i] < '0' || text[i] > '9') return std::nullopt;
    const auto pair = [&](size_t i) { return unsigned((text[i] - '0') * 10 + text[i + 1] - '0'); };
    Version result{pair(0), pair(3), pair(6)};
    if (result.month == 0 || result.month > 12) return std::nullopt;
    return result;
}
int compareVersion(Version a, Version b)
{
    const auto left = std::tie(a.year, a.month, a.patch);
    const auto right = std::tie(b.year, b.month, b.patch);
    return left < right ? -1 : (left > right ? 1 : 0);
}
}
