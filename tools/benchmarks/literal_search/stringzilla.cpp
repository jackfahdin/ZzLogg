#include "stringzilla.h"

// Compile only the relevant header kernels; every call below names its ISA explicitly.
#ifndef LITERAL_STRINGZILLA_HASWELL
#define LITERAL_STRINGZILLA_HASWELL 0
#endif
#define SZ_USE_HASWELL LITERAL_STRINGZILLA_HASWELL
#define SZ_USE_WESTMERE LITERAL_STRINGZILLA_HASWELL
#define SZ_USE_SKYLAKE 0
#define SZ_USE_ICELAKE 0
#define SZ_USE_GOLDMONT 0
#define SZ_USE_NEON 0
#define SZ_USE_SVE 0
#define SZ_USE_SVE2 0
#define SZ_USE_V128 0
#define SZ_USE_V128RELAXED 0
#define SZ_USE_RVV 0
#define SZ_USE_LASX 0
#define SZ_USE_POWERVSX 0
#include <stringzilla/find.h>

StringZillaKernel selectStringZilla(bool forceSerial)
{
#if LITERAL_STRINGZILLA_HASWELL
    // GCC/Clang CPU initialization has run before main. AVX2 support includes OS register-state support.
    static const bool available = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi")
        && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("lzcnt")
        && __builtin_cpu_supports("sse4.2");
    if (!forceSerial && available) return {sz_find_haswell, "StringZilla 5.3.2 haswell"};
#else
    (void)forceSerial;
#endif
    return {sz_find_serial, "StringZilla 5.3.2 serial"};
}
