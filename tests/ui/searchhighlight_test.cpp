#include <catch2/catch.hpp>
#include "highlighterset.h"
#include "quickfindpattern.h"

TEST_CASE( "Search highlights keep UTF-16 columns through tab expansion", "[highlight][search]" )
{
    // A byte-offset implementation would report 8 instead of 4 before expansion.
    const auto line = QString::fromUtf8( "中😀\tERROR error ERROR" );
    Highlighter highlighter;
    highlighter.setPattern( "ERROR" );
    highlighter.setUseRegex( false );
    highlighter.setForeColor( Qt::red );
    highlighter.setBackColor( Qt::yellow );
    klogg::vector<HighlightedMatch> matches;
    REQUIRE( highlighter.matchLine( line, matches ) );
    REQUIRE( matches.size() == 2 );
    REQUIRE( matches[0].startColumn() == 4_lcol );
    REQUIRE( matches[1].startColumn() == 16_lcol );
    HighlightedMatchRanges ranges{ matches };
    ranges.expandTabs( line );
    const auto expanded = ranges.matches();
    REQUIRE( expanded[0].startColumn() == 8_lcol );
    REQUIRE( expanded[1].startColumn() == 20_lcol );
    for ( const auto& match : expanded ) {
        REQUIRE( match.size() == 5_length );
        REQUIRE( match.foreColor() == QColor( Qt::red ) );
        REQUIRE( match.backColor() == QColor( Qt::yellow ) );
    }
    QuickFindMatcher quick{ true, QRegularExpression( "ERROR" ) };
    REQUIRE( quick.isLineMatching( line ) );
    REQUIRE( quick.getLastMatch() == std::make_pair( 4_lcol, 8_lcol ) );
    REQUIRE( quick.isLineMatchingBackward( line ) );
    REQUIRE( quick.getLastMatch() == std::make_pair( 16_lcol, 20_lcol ) );
}

TEST_CASE( "Highlight tab expansion covers tabs inside ranges and visible clipping", "[highlight]" )
{
    const QString line = "a\tb\tc";
    HighlightedMatchRanges ranges;
    ranges.addMatch( { 1_lcol, 2_length, Qt::red, Qt::yellow } );
    ranges.addMatch( { 4_lcol, 1_length, Qt::blue, Qt::white } );
    ranges.expandTabs( line );
    REQUIRE( ranges.front().startColumn() == 1_lcol );
    REQUIRE( ranges.front().size() == 8_length );
    REQUIRE( ranges.back().startColumn() == 16_lcol );
    ranges.clamp( 4_lcol, 16_lcol );
    REQUIRE( ranges.front().startColumn() == 4_lcol );
    REQUIRE( ranges.front().size() == 5_length );
    REQUIRE( ranges.back().size() == 1_length );
    HighlightedMatchRanges empty;
    empty.expandTabs( {} );
    REQUIRE( empty.empty() );
}

TEST_CASE( "Search regex highlights retain capture group positions", "[highlight][search]" )
{
    Highlighter highlighter;
    highlighter.setPattern( "(ERROR) (42)" );
    const auto line = QString::fromUtf8( "中😀\tERROR 42" );
    klogg::vector<HighlightedMatch> matches;
    REQUIRE( highlighter.matchLine( line, matches ) );
    REQUIRE( matches.size() == 2 );
    REQUIRE( matches[0].startColumn() == 4_lcol );
    REQUIRE( matches[0].size() == 5_length );
    REQUIRE( matches[1].startColumn() == 10_lcol );
    REQUIRE( matches[1].size() == 2_length );
    HighlightedMatchRanges ranges{ matches };
    ranges.expandTabs( line );
    REQUIRE( ranges.front().startColumn() == 8_lcol );
    REQUIRE( ranges.back().startColumn() == 14_lcol );
}
