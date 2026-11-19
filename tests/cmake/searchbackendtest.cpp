#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "hsregularexpression.h"

TEST_CASE( "Configured backend executes single and multiple pattern searches", "[backend]" )
{
    const klogg::vector<RegularExpressionPattern> patterns{
        RegularExpressionPattern{ QStringLiteral( "error" ) },
        RegularExpressionPattern{ QStringLiteral( "WARNING" ), false, false, false, false },
        RegularExpressionPattern{ QStringLiteral( "中文" ) }
    };
    HsRegularExpression expression{ patterns };
    REQUIRE( expression.isValid() );
    const auto matcher = expression.createMatcher();
#ifdef KLOGG_HAS_HS
    // This must actually exercise the accelerated library, not silently fall back.
    REQUIRE( std::holds_alternative<HsMultiMatcher>( matcher ) );
#else
    REQUIRE( std::holds_alternative<DefaultRegularExpressionMatcher>( matcher ) );
#endif
    const DefaultRegularExpressionMatcher reference{ patterns };
    for ( const auto& input :
          { std::string{}, std::string{ "error" }, std::string{ "中文 warning" },
            std::string{ "nothing" }, std::string{ "error 中文 WARNING" } } ) {
        CAPTURE( input );
        const auto actual
            = std::visit( [ & ]( const auto& m ) { return m.match( input ); }, matcher );
        REQUIRE( actual == reference.match( input ) );
    }

    HsRegularExpression single{ RegularExpressionPattern{ QStringLiteral( "needle" ) } };
    REQUIRE( single.isValid() );
    const auto singleMatcher = single.createMatcher();
#ifdef KLOGG_HAS_HS
    REQUIRE( std::holds_alternative<HsSingleMatcher>( singleMatcher ) );
#endif
    REQUIRE( std::visit( []( const auto& m ) { return m.match( "a needle here" ); }, singleMatcher )
             == MatchedPatterns{ 1 } );
    REQUIRE( std::visit( []( const auto& m ) { return m.match( "absent" ); }, singleMatcher )
             == MatchedPatterns{ 0 } );
}

TEST_CASE( "Unsupported accelerated syntax retains exact Qt semantics", "[backend]" )
{
    const klogg::vector<RegularExpressionPattern> patterns{ RegularExpressionPattern{
        QStringLiteral( "a(?=b)" ) } };
    HsRegularExpression expression{ patterns };
    REQUIRE( expression.isValid() );
    const auto matcher = expression.createMatcher();
#ifdef KLOGG_HAS_HS
    REQUIRE( ( std::holds_alternative<HsPrefilterMatcher>( matcher )
               || std::holds_alternative<DefaultRegularExpressionMatcher>( matcher ) ) );
#endif
    const DefaultRegularExpressionMatcher reference{ patterns };
    for ( const auto* input : { "ab", "ac", "a", "", "cab" } ) {
        CAPTURE( input );
        REQUIRE( std::visit( [ & ]( const auto& m ) { return m.match( input ); }, matcher )
                 == reference.match( input ) );
    }
    HsRegularExpression invalid{ RegularExpressionPattern{ QStringLiteral( "(" ) } };
    REQUIRE_FALSE( invalid.isValid() );
    REQUIRE_FALSE( invalid.errorString().isEmpty() );
}

TEST_CASE( "Prefilter confirms each candidate and owns independent regex state", "[backend]" )
{
    const klogg::vector<RegularExpressionPattern> patterns{
        RegularExpressionPattern{ QStringLiteral( "i2c(?=.*timeout)" ) },
        RegularExpressionPattern{ QStringLiteral( "binder(?! debug)" ) },
        RegularExpressionPattern{ QStringLiteral( "(?<=温度=)[0-9]+" ) },
        RegularExpressionPattern{ QStringLiteral( "ERROR(?=!)" ), false, false, false, false }
    };
    auto makeMatcher = [&] {
        HsRegularExpression expression{ patterns };
        REQUIRE( expression.isValid() );
        return expression.createMatcher();
    };
    const auto first = makeMatcher();
    const auto second = makeMatcher();
#ifdef KLOGG_HAS_HS
    REQUIRE( std::holds_alternative<HsPrefilterMatcher>( first ) );
    REQUIRE( std::holds_alternative<HsPrefilterMatcher>( second ) );
#endif
    const std::vector<std::pair<std::string, MatchedPatterns>> cases{
        { "i2c transfer timeout binder transaction 温度=42 error!", {1, 1, 1, 1} },
        { "i2c completed binder debug txn 温度=none ERROR?", {0, 0, 0, 0} },
        { "binder transaction", {0, 1, 0, 0} },
        { "温度=42℃ 😀", {0, 0, 1, 0} },
        { std::string{"i2c\0timeout", 11}, {1, 0, 0, 0} },
        { "", {0, 0, 0, 0} },
        { "unrelated", {0, 0, 0, 0} }
    };
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (const auto& [input, expected] : cases) {
            CAPTURE(input, repeat);
            const auto evaluate = [&](const auto& matcher) { return matcher.match(input); };
            REQUIRE( std::visit(evaluate, first) == expected );
            REQUIRE( std::visit(evaluate, second) == expected );
        }
    }
}
