#pragma once
#include "hsregularexpression.h"
#include <memory>
#include <optional>

namespace utf8regex {
using Patterns = klogg::vector<RegularExpressionPattern>;
struct Span {
    qsizetype start, length; // UTF-16 columns, as returned by Qt for display text.
    bool operator==(const Span& other) const { return start == other.start && length == other.length; }
};
struct Info { size_t programs=0, fallbackPatterns=0, jitPrograms=0, codeBytes=0, jitBytes=0; };
struct Routing { size_t direct=0, fallback=0, runtimeErrors=0; };
// Per-worker mutable scratch; distinct instances may be used concurrently.
// The same instance must not be scanned concurrently, even through const methods.
class Matcher {
public:
    explicit Matcher(const Patterns&);
    ~Matcher();
    Matcher(Matcher&&) noexcept;
    Matcher& operator=(Matcher&&) noexcept;
    Matcher(const Matcher&) = delete;
    Matcher& operator=(const Matcher&) = delete;
    MatchedPatterns match(std::string_view, Routing* = nullptr) const;
    std::optional<Span> firstMatch(size_t pattern, std::string_view, Routing* = nullptr) const;
    Info info() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void verify();
}
