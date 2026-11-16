#pragma once
#include <cstddef>

using LiteralFind = const char* (*)(const char*, std::size_t, const char*, std::size_t);
struct StringZillaKernel {
    LiteralFind find = nullptr;
    const char* name = "";
};
StringZillaKernel selectStringZilla(bool forceSerial);
