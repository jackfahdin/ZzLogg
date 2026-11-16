#pragma once
#include "regularexpressionpattern.h"
#include "containers.h"
#include <memory>
#include <string_view>
#include <vector>

using Patterns = klogg::vector<RegularExpressionPattern>;
using Lines = std::vector<std::string_view>;

class LiteralMatcher {
public:
    virtual ~LiteralMatcher() = default;
    // Row-major matrix: every line and every original pattern, including duplicates.
    virtual std::string scan(const Lines&) const = 0;
    virtual size_t automatonBytes() const { return 0; }
    virtual const char* implementation() const { return ""; }
};
std::unique_ptr<LiteralMatcher> makeMatcher(const std::string& backend, const Patterns&);
std::vector<std::string> backends();
void verifyContracts();
