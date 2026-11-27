/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include "regularexpression.h"
#include "booleansearchhighlighter.h"
#include <set>

SCENARIO( "Pattern matcher in boolean mode", "[patternmatcher]" )
{
    std::string_view matchLine = "\"This\" is matching pattern";

    WHEN( "Using single pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"matching\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern with ()" )
    {
        RegularExpression expression( RegularExpressionPattern(
            "(\"not_match\" | \"match\") & !(\"pattern\")", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE_FALSE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with escaped quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"\\\"This\\\"\"", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with not matched quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match", false, false, true, false ) );

        REQUIRE_FALSE( expression.isValid() );
    }
}

TEST_CASE("Combined highlight follows matching branches", "[patternmatcher][highlight]")
{
    struct Example { const char* expression; const char* line; std::set<std::string> highlights; };
    const Example examples[] = {
        {R"("alpha" and not("beta"))", "alpha", {"alpha"}},
        {R"("alpha" and not("beta"))", "alpha beta", {}},
        {R"("alpha" or "beta")", "alpha beta", {"alpha", "beta"}},
        {R"(("alpha" and "beta") or "gamma")", "alpha gamma", {"gamma"}},
        {R"(not("alpha"))", "beta", {}},
        {R"(not(not("alpha")))", "alpha", {"alpha"}},
        {R"(not("alpha" and not("beta")))", "alpha beta", {"beta"}},
        {R"("alpha" nand "beta")", "alpha", {}},
        {R"("alpha" nor "beta")", "gamma", {}},
        {R"("alpha" xor "beta")", "alpha", {"alpha"}},
        {R"("alpha" xor "beta")", "alpha beta", {}},
        {R"("alpha" xnor "beta")", "alpha beta", {"alpha", "beta"}},
        {R"("alpha" xnor "beta")", "gamma", {}},
        {R"("alpha" or "beta" and "gamma")", "alpha beta", {"alpha"}},
        {R"("alpha" AND NOT("beta"))", "ALPHA", {"alpha"}},
        {R"("alpha" & !("beta"))", "alpha", {"alpha"}},
        {R"("a.b" and not("beta"))", "a.b", {"a.b"}},
        {R"("a.b" and not("beta"))", "axb", {}},
        {R"("中文" or "beta")", "中文日志", {"中文"}},
        {R"("\"quoted\"" or "beta")", "\"quoted\"", {"\"quoted\""}},
    };
    for (const auto& example : examples) {
        INFO(example.expression);
        INFO(example.line);
        const RegularExpressionPattern pattern{example.expression, false, false, true, true};
        const RegularExpression expression{pattern};
        REQUIRE(expression.isValid());
        BooleanSearchHighlighter highlighter{pattern};
        std::set<std::string> actual;
        for (const auto index : highlighter.matchingPatternIndices(example.line))
            actual.insert(highlighter.patterns()[index].pattern.toStdString());
        REQUIRE(actual == example.highlights);
        if (!actual.empty()) REQUIRE(expression.createMatcher()->hasMatch(example.line));
    }
}

TEST_CASE("Combined highlight never paints a rejected line", "[patternmatcher][highlight]")
{
    for (const auto* first : {"and", "nand", "or", "nor", "xor", "xnor", "&", "|"}) {
        for (const auto* second : {"and", "nand", "or", "nor", "xor", "xnor", "&", "|"}) {
            const QString query = QString("\"alpha\" %1 \"beta\" %2 not(\"gamma\")").arg(first, second);
            INFO(query.toStdString());
            RegularExpressionPattern pattern{query, true, false, true, true};
            const RegularExpression expression{pattern};
            REQUIRE(expression.isValid());
            const auto matcher = expression.createMatcher();
            BooleanSearchHighlighter highlighter{pattern};
            for (unsigned mask = 0; mask < 8; ++mask) {
                const std::string line = std::string(mask & 1 ? "alpha " : "")
                    + (mask & 2 ? "beta " : "") + (mask & 4 ? "gamma" : "");
                INFO(line);
                const auto indices = highlighter.matchingPatternIndices(QString::fromStdString(line));
                if (!matcher->hasMatch(line)) REQUIRE(indices.empty());
                for (const auto index : indices) {
                    REQUIRE(index < highlighter.patterns().size());
                    REQUIRE(static_cast<QRegularExpression>(highlighter.patterns()[index])
                        .match(QString::fromStdString(line)).hasMatch());
                }
            }
        }
    }
}

TEST_CASE("Combined regex highlights respect regex and case options", "[patternmatcher][highlight]")
{
    const RegularExpressionPattern pattern{R"("ERR.R" and not("debug.*"))", true, false, true, false};
    BooleanSearchHighlighter highlighter{pattern};
    REQUIRE(highlighter.matchingPatternIndices("ERROR message").size() == 1);
    REQUIRE(highlighter.matchingPatternIndices("error message").empty());
    REQUIRE(highlighter.matchingPatternIndices("ERROR debug message").empty());
    BooleanSearchHighlighter invalid{RegularExpressionPattern{R"("alpha" and)", true, false, true, true}};
    REQUIRE(invalid.matchingPatternIndices("alpha").empty());
}

TEST_CASE("Symbolic not leaves literal exclamation marks intact", "[patternmatcher][highlight]")
{
    const RegularExpressionPattern pattern{R"("!alert" & !("debug!"))", true, false, true, true};
    const RegularExpression expression{pattern};
    REQUIRE(expression.isValid());
    const auto matcher = expression.createMatcher();
    REQUIRE(matcher->hasMatch("!alert"));
    REQUIRE_FALSE(matcher->hasMatch("!alert debug!"));
    BooleanSearchHighlighter highlighter{pattern};
    const auto indices = highlighter.matchingPatternIndices("!alert");
    REQUIRE(indices.size() == 1);
    REQUIRE(highlighter.patterns()[indices.front()].pattern == "!alert");
}
