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

#include <array>
#include <random>

namespace {
MatchedPatterns matchWithSeparateDecoding(
    const klogg::vector<RegularExpressionPattern>& patterns, std::string_view input )
{
    MatchedPatterns result;
    for ( const auto& pattern : patterns ) {
        const auto regexp = static_cast<QRegularExpression>( pattern );
        result.push_back( static_cast<char>(
            regexp.match( QString::fromUtf8( input.data(), klogg::isize( input ) ) ).hasMatch() ) );
    }
    return result;
}
} // namespace

TEST_CASE( "Qt matcher preserves every pattern result and its order", "[patternmatcher]" )
{
    const klogg::vector<RegularExpressionPattern> patterns{
        RegularExpressionPattern{ QStringLiteral( "error" ) },
        RegularExpressionPattern{ QStringLiteral( "中文😀" ) },
        RegularExpressionPattern{ QStringLiteral( "ERROR" ), false, false, false, false },
        RegularExpressionPattern{ QStringLiteral( "a.b" ), true, false, false, true },
        RegularExpressionPattern{ QStringLiteral( "a.b" ) },
        RegularExpressionPattern{ QStringLiteral( "^$" ) },
        RegularExpressionPattern{ QStringLiteral( "\\x{FFFD}" ) },
        RegularExpressionPattern{ QStringLiteral( "\\x{0}tail$" ) }
    };
    const DefaultRegularExpressionMatcher matcher{ patterns };
    const auto input = GENERATE(
        std::string{ "ERROR 中文😀 a.b" }, std::string{ "error axb" },
        std::string{ "nothing matches" }, std::string{}, std::string{ "\xFF" },
        std::string{ "prefix\0tail", 11 } );
    const auto actual = matcher.match( input );
    REQUIRE( actual == matchWithSeparateDecoding( patterns, input ) );
    REQUIRE( actual.size() == patterns.size() );

    if ( input == "ERROR 中文😀 a.b" ) {
        REQUIRE( actual == MatchedPatterns{ 0, 1, 1, 1, 1, 0, 0, 0 } );
    }
    else if ( input == "error axb" ) {
        REQUIRE( actual == MatchedPatterns{ 1, 0, 1, 0, 1, 0, 0, 0 } );
    }
    else if ( input.empty() ) {
        REQUIRE( actual == MatchedPatterns{ 0, 0, 0, 0, 0, 1, 0, 0 } );
    }
    else if ( input == "\xFF" ) {
        REQUIRE( actual == MatchedPatterns{ 0, 0, 0, 0, 0, 0, 1, 0 } );
    }
    else if ( input.size() == 11 ) {
        REQUIRE( actual == MatchedPatterns{ 0, 0, 0, 0, 0, 0, 0, 1 } );
    }
    else {
        REQUIRE( actual == MatchedPatterns( patterns.size(), 0 ) );
    }
}

TEST_CASE( "Qt matcher respects byte view boundaries and empty pattern lists", "[patternmatcher]" )
{
    const std::string bytes{ "prefix中文suffix" };
    const auto view = std::string_view{ bytes }.substr( 6, 6 );
    const DefaultRegularExpressionMatcher single{
        klogg::vector<RegularExpressionPattern>{ RegularExpressionPattern{ QStringLiteral( "^中文$" ) } } };
    REQUIRE( single.match( view ) == MatchedPatterns{ 1 } );
    REQUIRE( single.match( bytes ) == MatchedPatterns{ 0 } );

    const DefaultRegularExpressionMatcher empty{ klogg::vector<RegularExpressionPattern>{} };
    REQUIRE( empty.match( std::string_view{} ).empty() );
    REQUIRE( empty.match( bytes ).empty() );

    const klogg::vector<RegularExpressionPattern> patterns{
        RegularExpressionPattern{ QStringLiteral( "^$" ) },
        RegularExpressionPattern{ QStringLiteral( "\\x{FFFD}" ) }
    };
    const DefaultRegularExpressionMatcher matcher{ patterns };
    REQUIRE( matcher.match( std::string_view{} ) == MatchedPatterns{ 1, 0 } );
    // The view ends in the middle of 文: keep Qt's existing replacement semantics.
    const auto truncated = view.substr( 0, 5 );
    REQUIRE( matcher.match( truncated ) == MatchedPatterns{ 0, 1 } );
    REQUIRE( matcher.match( truncated ) == matchWithSeparateDecoding( patterns, truncated ) );
}

TEST_CASE( "Qt matcher keeps malformed UTF-8 replacement and repeated-call behavior",
           "[patternmatcher]" )
{
    const klogg::vector<RegularExpressionPattern> patterns{
        RegularExpressionPattern{ QStringLiteral( "\\x{FFFD}" ) },
        RegularExpressionPattern{ QStringLiteral( "\\x{0}" ) },
        RegularExpressionPattern{ QStringLiteral( "中文" ) },
        RegularExpressionPattern{ QStringLiteral( "\\p{L}+" ) },
        RegularExpressionPattern{ QStringLiteral( "^.{0,12}$" ) },
        RegularExpressionPattern{ QStringLiteral( "^$" ) }
    };
    const DefaultRegularExpressionMatcher matcher{ patterns };
    const std::array malformed{
        std::string{ "\xC0\xAF" }, std::string{ "\xED\xA0\x80" },
        std::string{ "\xF4\x90\x80\x80" }, std::string{ "\xE4\xB8" },
        std::string{ "\x80\xBF" }, std::string{ "a\0\xFFz", 4 }
    };
    for ( const auto& input : malformed ) {
        const auto expected = matchWithSeparateDecoding( patterns, input );
        REQUIRE( expected.front() == 1 );
        REQUIRE( matcher.match( input ) == expected );
    }

    std::mt19937 random{ 0x5dec0deU };
    for ( int sample = 0; sample < 256; ++sample ) {
        std::string input( random() % 129, '\0' );
        for ( auto& byte : input ) byte = static_cast<char>( random() & 0xFF );
        CAPTURE( sample );
        REQUIRE( matcher.match( input ) == matchWithSeparateDecoding( patterns, input ) );
    }
}

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
            "(\"not_match\" | \"match\") & not(\"pattern\")", false, false, true, false ) );
        REQUIRE( expression.isValid() );
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
