#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "zzlogg/update/manifest.h"
namespace zzlogg::update {
// Production: https only, port 443 only, exact allowed-host match.
// Test: additionally permits plain http on any port, so a locally signed feed
// can be served by a loopback test server. Test mode is only reachable from
// builds compiled with the local-feed option and an explicit runtime env opt-in.
bool isAllowedUpdateUrl(std::string_view url, const std::vector<std::string>& allowedHosts,
                        TrustEnvironment environment = TrustEnvironment::Production);
}
