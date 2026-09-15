#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace zzlogg::update {
bool isAllowedUpdateUrl(std::string_view url, const std::vector<std::string>& allowedHosts);
}
