#include "harness.h"
#include "configuration.h"
#include "highlighterset.h"
#include "quickfindpattern.h"
#include <iostream>

namespace workerprobe {
namespace {
using Positions = std::vector<std::pair<int64_t, int64_t>>;
void positions(const klogg::vector<HighlightedMatch>& matches, const Positions& expected)
{
    require(matches.size() == expected.size(), "Highlight count differs");
    for (size_t i = 0; i < expected.size(); ++i) {
        require(matches[i].startColumn().get() == expected[i].first
                    && matches[i].size().get() == expected[i].second,
                "Highlight UTF-16 position differs");
    }
}
void highlightCase(const QByteArray& bytes, const char* codec, const RegularExpressionPattern& query,
                   const Positions& rawPositions, const Positions& expandedPositions)
{
    Fixture file(bytes, codec);
    Configuration::get().setRegexpEnging(RegexpEngine::QRegularExpression);
    experimental::resetDiagnostics(true);
    const auto qt = search(file, query, false);
    experimental::resetDiagnostics(true);
    const auto candidate = search(file, query, true);
    require(qt.digest() == candidate.digest(), "Highlight input search differs from Qt");
    require(candidate.result.newMatches.cardinality() == 1
                && candidate.result.newMatches.contains(uint64_t{0}), "Highlight fixture row differs");
    require((candidate.diagnostics.matchers != 0) == (query.isPlainText && query.isCaseSensitive),
            "Unexpected highlight fixture search routing");
    const auto lines = file.log.getLines(0_lnum, 1_lcount);
    require(lines.size() == 1, "Missing display line");
    const auto& line = lines[0];
    Highlighter highlighter;
    highlighter.setPattern(query.pattern);
    highlighter.setUseRegex(!query.isPlainText);
    highlighter.setIgnoreCase(!query.isCaseSensitive);
    highlighter.setForeColor(Qt::black);
    highlighter.setBackColor(Qt::yellow);
    klogg::vector<HighlightedMatch> matches;
    require(highlighter.matchLine(line, matches), "Matched row has no Qt highlights");
    positions(matches, rawPositions);
    HighlightedMatchRanges ranges{matches};
    ranges.expandTabs(line);
    positions(ranges.matches(), expandedPositions);
    if (query.isPlainText) {
        QuickFindPattern quick;
        quick.changeSearchPattern(query.pattern, !query.isCaseSensitive, false);
        require(quick.matchLine(untabify(QString(line)), matches), "Quick find lost visible match");
        positions(matches, expandedPositions);
        const auto matcher = quick.getMatcher();
        require(matcher.isLineMatching(line), "Quick find forward missed");
        require(matcher.getLastMatch().first.get() == rawPositions.front().first,
                "Quick find forward position differs");
        require(matcher.isLineMatchingBackward(line), "Quick find backward missed");
        require(matcher.getLastMatch().first.get() == rawPositions.back().first,
                "Quick find backward position differs");
    }
}
void backendRoute(RegexpEngine engine)
{
    Configuration::get().setRegexpEnging(engine);
    Fixture file("ERROR\nok\nERROR blocked\n中文 ERROR\nab\nac\n");
    const std::vector<std::pair<RegularExpressionPattern, std::vector<uint64_t>>> cases{
        {{"ERROR", true, false, false, true}, {0, 2, 3}},
        {{"\"ERROR\" and not(\"blocked\")", true, false, true, true}, {0, 3}},
        {{"ERROR", true, true, false, true}, {1, 4, 5}},
        {{"error", false, false, false, true}, {0, 2, 3}},
        {{"a(?=b)", true, false, false, false}, {4}},
        {{"", true, false, false, true}, {0, 1, 2, 3, 4, 5}}
    };
    for (const auto& [query, expected] : cases) {
        for (const auto threads : {1U, 2U}) {
            const auto original = search(file, query, false, threads, 2);
            experimental::resetDiagnostics(true);
            const auto enabled = search(file, query, true, threads, 2);
            require(original.digest() == enabled.digest(), "Candidate switch changed selected backend result");
            SearchResultArray want;
            for (auto row : expected) want.add(row);
            require(enabled.result.newMatches == want, "Selected backend differs from fixed result");
            require(enabled.result.processedLines.get() == 6, "Selected backend lost progress");
            if (engine == RegexpEngine::Hyperscan) {
                require(enabled.diagnostics.matchers == 0 && enabled.diagnostics.batches == 0,
                        "Experimental memchr overrode selected accelerated backend");
#ifdef KLOGG_HAS_HS
                if (query.isPlainText && !query.pattern.isEmpty()) {
                    require(enabled.diagnostics.acceleratedMatchers == threads,
                            "Worker did not create the selected accelerated matchers");
                }
#endif
            }
            else {
                require(enabled.diagnostics.acceleratedMatchers == 0,
                        "Qt-selected worker used accelerated matchers");
            }
        }
    }
}
}
void verifyIntegration(bool requireAccelerated)
{
#ifndef KLOGG_HAS_HS
    require(!requireAccelerated, "Accelerated library required but this is a Qt-only build");
#else
    (void)requireAccelerated;
    HsRegularExpression single{RegularExpressionPattern{"ERROR"}};
    require(single.isValid(), "Accelerated single expression invalid");
    auto singleMatcher = single.createMatcher();
    require(std::holds_alternative<HsSingleMatcher>(singleMatcher), "Single matcher silently fell back to Qt");
    require(std::get<HsSingleMatcher>(singleMatcher).match("ERROR") == MatchedPatterns{1},
            "Accelerated single library did not match");
    HsRegularExpression multi{klogg::vector<RegularExpressionPattern>{
        RegularExpressionPattern{"ERROR"}, RegularExpressionPattern{"中文"}}};
    auto multiMatcher = multi.createMatcher();
    require(std::holds_alternative<HsMultiMatcher>(multiMatcher), "Multi matcher silently fell back to Qt");
    require(std::get<HsMultiMatcher>(multiMatcher).match("中文 ERROR") == MatchedPatterns({1, 1}),
            "Accelerated multi library did not match");
#endif
    Configuration::get().setQuickfindRegexpType(SearchRegexpType::ExtendedRegexp);
    const RegularExpressionPattern literal{"ERROR", true, false, false, true};
    const auto text = QString::fromUtf8("中😀\tERROR error ERROR\r\nabsent\n");
    for (const auto* codec : {"UTF-8", "UTF-16LE"}) {
        highlightCase(QTextCodec::codecForName(codec)->fromUnicode(text), codec, literal,
                      {{4, 5}, {16, 5}}, {{8, 5}, {20, 5}});
    }
    highlightCase(QTextCodec::codecForName("ISO-8859-1")->fromUnicode(QString::fromUtf8("é\tERROR\nabsent")),
                  "ISO-8859-1", literal, {{2, 5}}, {{8, 5}});
    highlightCase(QByteArray("a\0", 2) + "😀\tERROR\nabsent", "UTF-8", literal, {{5, 5}}, {{8, 5}});
    highlightCase(QByteArray("\xFF\tERROR\nabsent"), "UTF-8", literal, {{2, 5}}, {{8, 5}});
    highlightCase(QByteArray("\xEF\xBB\xBF" "中😀\tERROR\nabsent"), "UTF-8", literal, {{4, 5}}, {{8, 5}});
    highlightCase("中😀\ta.b aXb a.b\nabsent", "UTF-8", {"a.b", true, false, false, true},
                  {{4, 3}, {12, 3}}, {{8, 3}, {16, 3}});
    highlightCase("中😀\tERROR 42\nabsent", "UTF-8", {"(ERROR) (42)", true, false, false, false},
                  {{4, 5}, {10, 2}}, {{8, 5}, {14, 2}});
    highlightCase("中😀\terror ERROR\nabsent", "UTF-8", {"ERROR", false, false, false, true},
                  {{4, 5}, {10, 5}}, {{8, 5}, {14, 5}});
    backendRoute(RegexpEngine::QRegularExpression);
    backendRoute(RegexpEngine::Hyperscan);
    Configuration::get().setRegexpEnging(RegexpEngine::QRegularExpression);
    experimental::resetDiagnostics(false);
#ifdef KLOGG_HAS_HS
    std::cout << "Highlight/routing contracts passed; actual accelerated single/multi matchers executed\n";
#else
    std::cout << "Highlight/routing contracts passed; Qt-only fallback verified (no accelerated library)\n";
#endif
}
}
