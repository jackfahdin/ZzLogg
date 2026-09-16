// Parse the host Windows SDK and standard headers before describing a different
// target. These are controlled host tests, not native ARM executions.
#include "stablepackage_p.h"
#include "authenticode_p.h"
#if defined(ZZLOGG_TEST_ARM64EC)
#define _M_ARM64EC 1
#else
#define _M_ARM64 1
#endif
#include "packageverification.cpp"
int main() {
    const auto result=zzlogg::update::verifyPackageForExecution({}, {}, {}, L"C:\\unused.exe");
    return !result.package && result.error==zzlogg::update::PackageVerificationError::Unsupported?0:1;
}
