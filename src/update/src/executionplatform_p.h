#pragma once
namespace zzlogg::update::detail {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)) \
    && !defined(_M_ARM64) && !defined(_M_ARM64EC)
inline constexpr bool executionPlatformSupported=true;
#else
inline constexpr bool executionPlatformSupported=false;
#endif
}
