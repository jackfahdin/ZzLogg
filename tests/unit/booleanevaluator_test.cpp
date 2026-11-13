#include <catch2/catch.hpp>

#include "booleanevaluator.h"
#include "regularexpression.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <random>

namespace {
klogg::vector<RegularExpressionPattern> patternsFor( size_t count )
{
    klogg::vector<RegularExpressionPattern> patterns;
    for ( size_t i = 0; i < count; ++i )
        patterns.emplace_back( QString::number( i ) );
    return patterns;
}

std::string substitute( std::string source,
                        const klogg::vector<RegularExpressionPattern>& patterns )
{
    // Descending indices also support $10 without replacing its $1 prefix first.
    for ( size_t i = patterns.size(); i-- > 0; ) {
        const auto token = "$" + std::to_string( i );
        for ( auto pos = source.find( token ); pos != std::string::npos;
              pos = source.find( token ) ) {
            source.replace( pos, token.size(), patterns[ i ].id() );
        }
    }
    return source;
}

std::string variablesFor( unsigned combination, size_t count )
{
    std::string variables( count, 0 );
    for ( size_t i = 0; i < count; ++i )
        variables[ i ] = static_cast<char>( ( combination >> i ) & 1U );
    return variables;
}

struct Generated {
    std::string expression;
    uint64_t truth;
};

Generated generate( std::mt19937& random, int depth, unsigned count = 6 )
{
    static const std::vector<std::string> ops{ " and ", " nand ", " or ", " nor ",
                                               " xor ", " xnor ", " & ",  " | " };
    if ( depth == 0 || random() % 4 == 0 ) {
        const auto variable = static_cast<unsigned>( random() % count );
        uint64_t truth = 0;
        for ( unsigned input = 0; input < 64; ++input ) {
            if ( ( input >> variable ) & 1U )
                truth |= uint64_t{ 1 } << input;
        }
        return { "$" + std::to_string( variable ), truth };
    }
    if ( random() % 4 == 0 ) {
        auto child = generate( random, depth - 1, count );
        return { "not(" + child.expression + ")", ~child.truth };
    }
    auto left = generate( random, depth - 1, count );
    const auto op = random() % ops.size();
    auto right = generate( random, depth - 1, count );
    uint64_t truth = 0;
    switch ( op ) {
    case 0:
    case 6:
        truth = left.truth & right.truth;
        break;
    case 1:
        truth = ~( left.truth & right.truth );
        break;
    case 2:
    case 7:
        truth = left.truth | right.truth;
        break;
    case 3:
        truth = ~( left.truth | right.truth );
        break;
    case 4:
        truth = left.truth ^ right.truth;
        break;
    case 5:
        truth = ~( left.truth ^ right.truth );
        break;
    }
    return { "(" + left.expression + ops[ op ] + right.expression + ")", truth };
}
} // namespace

TEST_CASE( "Boolean evaluator preserves accepted ExprTk truth tables", "[boolean][legacy]" )
{
    QFile file( QString::fromUtf8( BOOLEAN_COMPATIBILITY_FIXTURE ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto document = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( document.isObject() );
    int tested = 0;
    for ( const auto& value : document.object()[ "cases" ].toArray() ) {
        const auto row = value.toObject();
        if ( !row[ "valid" ].toBool() || !row[ "public_grammar" ].toBool() )
            continue;
        const auto count = static_cast<size_t>( row[ "patterns" ].toInt() );
        auto patterns = patternsFor( count );
        const auto source = row[ "source" ].toString().toStdString();
        CAPTURE( source );
        BooleanExpressionEvaluator evaluator( substitute( source, patterns ), patterns );
        REQUIRE( evaluator.isValid() );
        const auto truth = row[ "truth" ].toString().toStdString();
        REQUIRE( truth.size() == ( 1U << count ) );
        for ( unsigned input = 0; input < truth.size(); ++input ) {
            REQUIRE( evaluator.evaluate( variablesFor( input, count ) )
                     == ( truth[ input ] == '1' ) );
        }
        ++tested;
    }
    REQUIRE( tested > 200 );
}

TEST_CASE( "Boolean operators compose according to independent truth tables", "[boolean]" )
{
    for ( unsigned count : { 1U, 4U, 6U } ) {
        auto patterns = patternsFor( count );
        std::mt19937 random( 0x6b001U );
        for ( int sample = 0; sample < 256; ++sample ) {
            const auto generated = generate( random, 5, count );
            CAPTURE( count, sample, generated.expression );
            BooleanExpressionEvaluator evaluator( substitute( generated.expression, patterns ),
                                                  patterns );
            INFO( evaluator.errorString() );
            REQUIRE( evaluator.isValid() );
            for ( unsigned input = 0; input < ( 1U << count ); ++input ) {
                REQUIRE( evaluator.evaluate( variablesFor( input, count ) )
                         == ( ( generated.truth >> input ) & 1U ) );
            }
        }
    }
}

TEST_CASE( "Boolean syntax rejects unsupported and incomplete expressions", "[boolean]" )
{
    auto patterns = patternsFor( 3 );
    const auto source = GENERATE( "", "()", "not()", "not $0", "!($0)", "$0 && $1", "$0 || $1",
                                  "($0", "$0)", "$0 and", "and $0", "$0 & & $1", "$0 not($1)",
                                  "$0 ||| $1", "p_999999", "$0and$1" );
    BooleanExpressionEvaluator evaluator( substitute( source, patterns ), patterns );
    REQUIRE_FALSE( evaluator.isValid() );
    REQUIRE_FALSE( evaluator.errorString().empty() );
    REQUIRE( evaluator.errorString().find( " at " ) != std::string::npos );
    REQUIRE_FALSE( evaluator.evaluate( variablesFor( 7, 3 ) ) );
}

TEST_CASE( "Boolean operator precedence and associativity remain explicit", "[boolean]" )
{
    const std::vector<std::string> operators{ "and", "nand", "or", "nor", "xor", "xnor", "&", "|" };
    const auto precedence
        = []( const std::string& op ) { return op == "and" || op == "nand" || op == "&" ? 2 : 1; };
    const auto apply = []( const std::string& op, bool left, bool right ) {
        if ( op == "and" || op == "&" )
            return left && right;
        if ( op == "nand" )
            return !( left && right );
        if ( op == "or" || op == "|" )
            return left || right;
        if ( op == "nor" )
            return !( left || right );
        if ( op == "xor" )
            return left != right;
        return left == right;
    };
    auto patterns = patternsFor( 3 );
    for ( const auto& first : operators ) {
        for ( const auto& second : operators ) {
            const auto source = "$0 " + first + " $1 " + second + " $2";
            CAPTURE( source );
            BooleanExpressionEvaluator evaluator( substitute( source, patterns ), patterns );
            REQUIRE( evaluator.isValid() );
            for ( unsigned input = 0; input < 8; ++input ) {
                const auto variables = variablesFor( input, 3 );
                const bool a = variables[ 0 ] != 0, b = variables[ 1 ] != 0,
                           c = variables[ 2 ] != 0;
                const bool expected = precedence( second ) > precedence( first )
                                          ? apply( first, a, apply( second, b, c ) )
                                          : apply( second, apply( first, a, b ), c );
                REQUIRE( evaluator.evaluate( variables ) == expected );
            }
        }
    }
}

TEST_CASE( "Boolean search preserves public query and inversion semantics", "[boolean]" )
{
    const auto inverse = GENERATE( false, true );
    RegularExpression expression(
        RegularExpressionPattern{ QStringLiteral( "(\"中文\" AND not(\"排除\")) | \"a\\\"b\"" ),
                                  false, inverse, true, true } );
    REQUIRE( expression.isValid() );
    auto matcher = expression.createMatcher();
    for ( const auto& [ line, expected ] :
          std::vector<std::pair<std::string, bool>>{ { "中文日志", true },
                                                     { "中文排除", false },
                                                     { "a\"b", true },
                                                     { "A\"B", true },
                                                     { "nothing", false },
                                                     { "", false } } ) {
        REQUIRE( matcher->hasMatch( line ) == ( expected != inverse ) );
    }
    for ( const auto* query : { "\"x\" + \"y\"", "1 and \"x\"", "\"x\"; \"y\"", "\"x\"\tand\t\"y\"",
                                "\"unclosed", "unquoted" } ) {
        RegularExpression invalid( RegularExpressionPattern{ query, true, false, true, true } );
        REQUIRE_FALSE( invalid.isValid() );
        REQUIRE_FALSE( invalid.errorString().isEmpty() );
    }
}

TEST_CASE( "Boolean evaluator bounds expression size and nesting", "[boolean][limits]" )
{
    auto patterns = patternsFor( 1 );
    const auto id = patterns.front().id();
    const std::string atLimit = std::string( 65536 - id.size(), ' ' ) + id;
    BooleanExpressionEvaluator accepted( atLimit, patterns );
    REQUIRE( accepted.isValid() );
    BooleanExpressionEvaluator oversized( " " + atLimit, patterns );
    REQUIRE_FALSE( oversized.isValid() );
    REQUIRE( oversized.errorString().find( "65536" ) != std::string::npos );

    BooleanExpressionEvaluator nested( std::string( 256, '(' ) + id + std::string( 256, ')' ),
                                       patterns );
    REQUIRE( nested.isValid() );
    REQUIRE( nested.evaluate( variablesFor( 1, 1 ) ) );
    REQUIRE_FALSE( nested.evaluate( variablesFor( 0, 1 ) ) );
    auto notExpression = id;
    for ( int depth = 0; depth < 256; ++depth )
        notExpression = "not(" + notExpression + ")";
    BooleanExpressionEvaluator nestedNot( notExpression, patterns );
    REQUIRE( nestedNot.isValid() );
    REQUIRE( nestedNot.evaluate( variablesFor( 1, 1 ) ) );
    BooleanExpressionEvaluator tooDeep( std::string( 257, '(' ) + id + std::string( 257, ')' ),
                                        patterns );
    REQUIRE_FALSE( tooDeep.isValid() );
    REQUIRE( tooDeep.errorString().find( "256" ) != std::string::npos );

    auto excessivePatterns = patternsFor( 4097 );
    BooleanExpressionEvaluator tooMany( excessivePatterns.front().id(), excessivePatterns );
    REQUIRE_FALSE( tooMany.isValid() );
    REQUIRE( tooMany.errorString().find( "4096" ) != std::string::npos );

    RegularExpression tooLongQuery( RegularExpressionPattern{
        QStringLiteral( "\"" ) + QString( 65536, QChar( 'x' ) ) + QStringLiteral( "\"" ), true,
        false, true, true } );
    REQUIRE_FALSE( tooLongQuery.isValid() );
    const auto maximumQuery
        = QStringLiteral( "\"x\"|" ).repeated( 4095 ) + QStringLiteral( "\"x\"" );
    RegularExpression atPatternLimit(
        RegularExpressionPattern{ maximumQuery, true, false, true, true } );
    REQUIRE( atPatternLimit.isValid() );
    RegularExpression overPatternLimit( RegularExpressionPattern{
        maximumQuery + QStringLiteral( "|\"x\"" ), true, false, true, true } );
    REQUIRE_FALSE( overPatternLimit.isValid() );
    REQUIRE( overPatternLimit.errorString().contains( QStringLiteral( "4096" ) ) );
}

TEST_CASE( "Boolean evaluators keep independent state and validate value counts", "[boolean]" )
{
    auto patterns = patternsFor( 6 );
    BooleanExpressionEvaluator first( substitute( "$0 & not($1) | $5", patterns ), patterns );
    BooleanExpressionEvaluator second( substitute( "$0 & not($1) | $5", patterns ), patterns );
    REQUIRE( first.isValid() );
    REQUIRE( second.isValid() );
    for ( unsigned input = 0; input < 64; ++input ) {
        const auto variables = variablesFor( input, 6 );
        const bool expected = ( variables[ 0 ] && !variables[ 1 ] ) || variables[ 5 ];
        REQUIRE( first.evaluate( variables ) == expected );
        REQUIRE( second.evaluate( variablesFor( 63 - input, 6 ) )
                 == ( ( ( ( 63 - input ) & 1U ) && !( ( 63 - input ) & 2U ) )
                      || ( ( 63 - input ) & 32U ) ) );
    }
    REQUIRE_FALSE( first.evaluate( "" ) );
    REQUIRE_FALSE( first.evaluate( std::string( 7, 1 ) ) );
}
