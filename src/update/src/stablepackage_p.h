#pragma once
#ifndef _WIN32
#error The execution verification backend requires Windows.
#endif
#include "zzlogg/update/packageverification.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace zzlogg::update::detail {
// Private Windows boundary. This lease never grants execution eligibility.
class StablePackage {
public:
    StablePackage();
    ~StablePackage();
    StablePackage(StablePackage&&) noexcept;
    StablePackage& operator=(StablePackage&&) noexcept;
    PackageVerificationError open(const std::wstring&);
    PackageVerificationError verifyContent(std::uint64_t, const std::string&);
    bool identityUnchanged() const;
    HANDLE handle() const;
    const std::wstring& path() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
