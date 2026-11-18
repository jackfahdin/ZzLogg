#include "pipeline.h"
#include <iostream>
#include <stdexcept>
namespace pipeline {
namespace {
const std::vector<std::string> backends{"qt", "memchr-line", "memchr-128", "memchr-4096"};
void require(bool good, const char* message)
{
    if (!good) throw std::runtime_error(message);
}
// Fixed expected line numbers catch inversion, bit order, fallback and transcoding errors.
void check(const Query& q, const std::vector<std::string>& lines,
           const std::vector<uint64_t>& expected, const char* encoding = "UTF-8",
           const QString& prefilter = {}, bool finalNewline = true)
{
    SearchResultArray want;
    for (const auto row : expected) want.add(row);
    std::string oracle;
    for (const auto& backend : backends) {
        auto raw = rawLines(lines, encoding, prefilter, finalNewline);
        Pipeline p(q, backend);
        auto result = p.run(raw);
        require(result.matches == want, "fixed line-number contract failed");
        if (backend == "qt") oracle = result.digest();
        else require(result.digest() == oracle, "PatternMatcher result/width/count mismatch");
        require(p.run(raw).digest() == result.digest(), "repeated scan changed results");
    }
}
}
void verify()
{
    const auto query = Query::make({"ERROR"});
    for (const auto& backend : backends) {
        auto raw = rawLines({"ok", "ERROR\tx", "ERROR"});
        Pipeline p(query, backend);
        auto result = p.run(raw);
        require(result.matches.cardinality() == 2 && result.matches.contains(uint64_t{1})
                    && result.matches.contains(uint64_t{2}) && result.maxLength.get() == 9
                    && result.processed == 3, "fixed line numbers/width contract failed");
        auto empty = rawLines({});
        require(p.run(empty).processed == 0, "empty input processed rows");
        auto cancelled = p.run(raw, {false, true, false});
        require(cancelled.cancelled && cancelled.processed == 0 && cancelled.converted == 0
                    && cancelled.matches.isEmpty(), "pre-cancel must skip conversion and matching");
        std::vector<std::string> many(259, "ERROR");
        auto block = rawLines(many);
        const auto full = p.run(block);
        require(full.matches.cardinality() == 259 && full.matches.contains(uint64_t{258})
                    && full.processed == 259, "partial last batch lost rows");
        const auto stopped = p.run(block, {true, false, true});
        const size_t expected = backend == "memchr-line" ? 1 : backend == "memchr-128" ? 128 : 259;
        require(stopped.cancelled && stopped.processed == expected
                    && stopped.matches.cardinality() == expected, "batch cancellation boundary mismatch");
        require(p.run(block).digest() == full.digest(), "cancel state leaked to next scan");
        const auto profiled = p.run(block, {true, false, false});
        require(profiled.digest() == full.digest(), "profiling changed results");
    }
    const std::vector<std::string> truth{"none", "a", "b", "ab"};
    const std::vector<std::pair<QString, std::vector<uint64_t>>> operators{
        {"and", {3}}, {"or", {1, 2, 3}}, {"xor", {1, 2}}, {"nand", {0, 1, 2}},
        {"nor", {0}}, {"xnor", {0, 3}}, {"&", {3}}, {"|", {1, 2, 3}}};
    for (const auto& [op, expected] : operators) {
        check(Query::make({"a", "b"}, op), truth, expected);
        std::vector<uint64_t> inverse;
        for (uint64_t i = 0; i < 4; ++i)
            if (std::find(expected.begin(), expected.end(), i) == expected.end()) inverse.push_back(i);
        check(Query::make({"a", "b"}, op, true), truth, inverse);
    }
    check(Query::make({"a", "aa", "a"}, "and"), {"aaa", "a", "none"}, {0});
    check(Query::make({"a\"b", "tail"}, "or"), {"a\"b", "tail", "none"}, {0, 1});
    check(Query::make({"ERROR"}, {}, true), {"ok", "ERROR"}, {0});
    check(Query::make({"ä"}, {}, false, false), {"Ä", "ä", "x"}, {0, 1});
    check(Query::make({"a.b"}, {}, false, true, false), {"axb", "a.b", "ab"}, {0, 1});
    check(Query::make({""}), {"", "abc"}, {0, 1});
    check(Query::make({"a", ""}, "and"), {"", "a", "x"}, {1});
    check(Query::make({QString::fromUtf8("�")}), {std::string("\xFF"), "ok"}, {0});
    check(Query::make({QString(QChar(0xFEFF))}), {"\xEF\xBB\xBF" "a", "a\xEF\xBB\xBF"}, {1});
    check(Query::make({QString::fromUtf8("x\0y", 3)}), {std::string("x\0y", 3), "xy"}, {0});
    for (const auto* encoding : {"UTF-8", "UTF-16LE"})
        check(Query::make({"中文😀"}), {"ok", "中文😀\tx", "中文😀\r", "中文😀"},
              {1, 2, 3}, encoding, {}, false);
    check(Query::make({"é"}), {"café", "ok"}, {0}, "ISO-8859-1");
    check(Query::make({"ERROR"}), {"[tag]ERROR", "ok", "ERROR"}, {0, 2}, "UTF-8", "\\[tag\\]");
    check(Query::make({"ERROR"}), {"ERR", "OR"}, {0}, "UTF-8", "\n", false);
    bool rejected = false;
    try { Pipeline bad(query, "missing"); } catch (const std::exception&) { rejected = true; }
    require(rejected, "unavailable backend accepted");
    std::cout << "Pipeline contracts passed: fixed rows/width, 8 operators, inverse, overlap, Unicode/regex/empty fallback, "
                 "UTF-8/UTF-16LE/Latin-1, BOM/NUL/invalid bytes, prefilter, tail batches and cancellation boundaries\n";
}
}
