#include "matcher.h"
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>

namespace utf8regex {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
RegularExpressionPattern regex(const QString& p, bool sensitive = true) { return {p, sensitive, false, false, false}; }
void fixed(const RegularExpressionPattern& pattern, std::string_view text,
           std::optional<Span> expected, bool direct = true)
{
    const Patterns patterns{pattern};
    Matcher candidate(patterns);
    const DefaultRegularExpressionMatcher reference(patterns);
    Routing routing;
    require(candidate.match(text, &routing) == MatchedPatterns{static_cast<char>(expected.has_value())}, "Fixed bit differs");
    require(candidate.match(text) == reference.match(text), "Fixed Qt bit differs");
    require(candidate.firstMatch(0, text) == expected, "Fixed UTF-16 span differs");
    const auto qt = static_cast<QRegularExpression>(pattern).match(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
    if (expected) require(Span{qt.capturedStart(), qt.capturedLength()} == *expected, "Fixed Qt span differs");
    require(routing.direct == size_t(direct) && routing.fallback == size_t(!direct), "Unexpected fixed route");
}
}
void verify()
{
    fixed(regex("ERROR"), "中😀\tERROR", Span{4, 5});
    fixed(regex("^.$"), "😀", Span{0, 2});
    fixed(regex("^.$"), "中文", {});
    fixed(regex("(?<=温度=)[0-9]+"), "😀温度=42℃", Span{5, 2});
    fixed(regex("a(?=b)"), "cab", Span{1, 1});
    fixed(regex("a(?=b)"), "ac", {});
    fixed(regex("binder(?! debug)"), "binder debug", {});
    fixed(regex("(?<x>温度)\\k<x>"), "温度温度", Span{0, 4});
    fixed(regex("\\b\\p{L}+\\b"), "42 中文!", Span{3, 2});
    fixed(regex("k", false), "K", Span{0, 1});
    fixed(regex("^\\x{10400}$", false), "𐐨", Span{0, 2});
    fixed(regex(""), "😀", Span{0, 0});
    fixed(regex("$"), "😀", Span{2, 0});
    fixed(regex("^$"), std::string_view{}, Span{0, 0});
    fixed(regex("^ERROR$"), "ERROR\r", {});
    fixed(regex("\\R"), "a\r\nb", Span{1, 2});
    fixed(regex("(?m)^b"), "a\nb", Span{2, 1});
    fixed(regex("\\x{0}tail$"), std::string_view("a\0tail", 6), Span{1, 5});
    fixed(regex("^\\x{FFFD}$"), std::string_view("\xff", 1), Span{0, 1}, false);
    fixed(regex("^ERROR"), "\xef\xbb\xbf" "ERROR", Span{0, 5}, false);
    fixed(regex("\\x{FEFF}"), "a\xef\xbb\xbfz", Span{1, 1});
    fixed(regex("^\\C\\C$"), "😀", Span{0, 2}, false);
    fixed({"\\C", true, false, false, true}, "\\C", Span{0, 2});
    fixed({"a.b", true, false, false, true}, "a.b", Span{0, 3});
    const std::string surrounding = "xxx中😀ERRORsuffix";
    fixed(regex("^中😀ERROR$"), std::string_view(surrounding).substr(3, 12), Span{0, 8});

    const Patterns patterns{regex("ERROR"), regex("^$"), regex("\\x{FFFD}"), regex("\\x{0}"),
        regex("\\p{L}+"), regex(".{2,5}"), regex("\\C"), regex("(?<x>a)\\k<x>"), regex("ERROR")};
    Matcher candidate(patterns);
    const DefaultRegularExpressionMatcher reference(patterns);
    std::mt19937 random{20261119U};
    for (int n = 0; n < 512; ++n) {
        std::string line(random() % 129, '\0');
        for (auto& c : line) c = static_cast<char>(random() & 255);
        require(candidate.match(line) == reference.match(line), "Random byte result differs");
    }
    for (const auto& line : {std::string("ERROR ERROR"), std::string(1048600, 'a'), std::string("中文 😀 aa")})
        require(candidate.match(line) == reference.match(line), "Repeated/long result differs");
    auto moved = std::move(candidate);
    Matcher other({regex("unrelated")});
    other = std::move(moved);
    require(other.match("ERROR") == reference.match("ERROR"), "Moved matcher lost ownership");
    Matcher empty(Patterns{});
    require(empty.match("anything").empty(), "Empty pattern list differs");
    bool rejected = false;
    try { Matcher invalid({regex("(")}); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Qt-invalid query was accepted");
    Matcher limited({regex("(*LIMIT_MATCH=1)(*NO_JIT)(*NO_START_OPT)^(a+)+b$")});
    Routing limitedRoute;
    require(limited.match("aaaaaaaaac", &limitedRoute) == MatchedPatterns{0}, "Limited query result differs");
    require(limitedRoute.runtimeErrors == 1 && limitedRoute.fallback == 1,
            "PCRE runtime error must use Qt fallback");
    require(limited.info().jitPrograms == 0, "NO_JIT query incorrectly reported JIT code");
    const auto info = other.info();
    require(info.programs == patterns.size() - 1 && info.fallbackPatterns == 1, "Programs/fallback count differs");
    require(info.jitPrograms > 0 && info.codeBytes > 0 && info.jitBytes > 0, "Actual PCRE JIT code required for this experiment");
    std::cout << "UTF-8 regex contracts passed: fixed bits/UTF-16 spans/routes, 512 random inputs, long lines and ownership\n";
}
}
