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
