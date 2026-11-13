#include "booleanevaluator.h"
#include "regularexpression.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <random>
#ifdef __linux__
#include <dlfcn.h>
#endif

namespace {
std::string substitute( std::string expression,
                        const klogg::vector<RegularExpressionPattern>& patterns )
{
    for ( size_t i = 0; i < patterns.size(); ++i ) {
        const auto token = "$" + std::to_string( i );
        for ( auto at = expression.find( token ); at != std::string::npos;
              at = expression.find( token ) ) {
            expression.replace( at, token.size(), patterns[ i ].id() );
        }
    }
    return expression;
}

std::string randomExpression( std::mt19937& random, int depth )
{
    static const std::vector<std::string> ops{ " and ", " nand ", " or ", " nor ",
                                               " xor ", " xnor ", " & ",  " | " };
    if ( depth == 0 || random() % 4 == 0 )
        return "$" + std::to_string( random() % 6 );
    if ( random() % 4 == 0 )
        return "not(" + randomExpression( random, depth - 1 ) + ")";
    auto left = randomExpression( random, depth - 1 );
    auto op = ops[ random() % ops.size() ];
    auto right = randomExpression( random, depth - 1 );
    return "(" + left + op + right + ")";
}

QJsonArray benchmark()
{
    QJsonArray rows;
    void ( *beginAllocations )() = nullptr;
    void ( *endAllocations )() = nullptr;
    uint64_t ( *allocationCalls )() = nullptr;
#ifdef __linux__
    beginAllocations = reinterpret_cast<void ( * )()>( dlsym( RTLD_DEFAULT, "regex_alloc_begin" ) );
    endAllocations = reinterpret_cast<void ( * )()>( dlsym( RTLD_DEFAULT, "regex_alloc_end" ) );
    allocationCalls
        = reinterpret_cast<uint64_t ( * )()>( dlsym( RTLD_DEFAULT, "regex_alloc_calls" ) );
#endif
    for ( const size_t count : { 1U, 3U, 4U, 6U, 16U, 64U } ) {
        klogg::vector<RegularExpressionPattern> patterns;
        for ( size_t i = 0; i < count; ++i )
            patterns.emplace_back( QString::number( i ) );
        for ( const auto* op : { "and", "&", "or", "|" } ) {
            std::string expression = patterns.front().id();
            for ( size_t i = 1; i < count; ++i )
                expression += " " + std::string( op ) + " " + patterns[ i ].id();
            QElapsedTimer timer;
            timer.start();
            for ( int repetition = 0; repetition < 32; ++repetition ) {
                BooleanExpressionEvaluator instance( expression, patterns );
                if ( !instance.isValid() )
                    throw std::runtime_error( instance.errorString() );
            }
            const double constructionNs = static_cast<double>( timer.nsecsElapsed() ) / 32;
            BooleanExpressionEvaluator evaluator( expression, patterns );
            std::vector<std::string> inputs;
            std::vector<bool> expected;
            for ( size_t input = 0; input < 32; ++input ) {
                std::string values( count, 0 );
                for ( size_t i = 0; i < count; ++i ) {
                    values[ i ] = static_cast<char>( input == 0
                                                     || ( input > 1 && ( input + i ) % 3 == 0 ) );
                }
                const bool isAnd = std::string( op ) == "and" || std::string( op ) == "&";
                expected.push_back( isAnd ? std::all_of( values.begin(), values.end(),
                                                         []( char v ) { return v != 0; } )
                                          : std::any_of( values.begin(), values.end(),
                                                         []( char v ) { return v != 0; } ) );
                if ( evaluator.evaluate( values ) != expected.back() )
                    throw std::runtime_error( "Truth mismatch" );
                inputs.push_back( std::move( values ) );
            }
            uint64_t checksum = 0;
            constexpr size_t iterations = 100000;
            for ( size_t i = 0; i < 1024; ++i )
                evaluator.evaluate( inputs[ i % inputs.size() ] );
            if ( beginAllocations )
                beginAllocations();
            timer.restart();
            for ( size_t i = 0; i < iterations; ++i )
                checksum += evaluator.evaluate( inputs[ i % inputs.size() ] );
            const double evaluationNs = static_cast<double>( timer.nsecsElapsed() ) / iterations;
            if ( endAllocations )
                endAllocations();
            uint64_t expectedChecksum = 0;
            for ( size_t i = 0; i < iterations; ++i )
                expectedChecksum += expected[ i % expected.size() ];
            if ( checksum != expectedChecksum )
                throw std::runtime_error( "Checksum mismatch" );
            QJsonObject row{ { "patterns", static_cast<int>( count ) },
                             { "operator", op },
                             { "construct_ns", constructionNs },
                             { "evaluate_ns", evaluationNs },
                             { "checksum", static_cast<double>( checksum ) } };
            if ( allocationCalls )
                row.insert( "evaluation_allocations", static_cast<double>( allocationCalls() ) );
            rows.append( row );
        }
    }
    return rows;
}
} // namespace

int main( int argc, char** argv )
{
    QCoreApplication app( argc, argv );
    if ( app.arguments().size() != 3
         || ( app.arguments()[ 1 ] != "--cases" && app.arguments()[ 1 ] != "--benchmark" ) ) {
        std::cerr << "Usage: zzlogg_boolean_probe --cases|--benchmark output.json\n";
        return 2;
    }
    if ( app.arguments()[ 1 ] == "--benchmark" ) {
        const auto results = benchmark();
        QFile file( app.arguments()[ 2 ] );
        if ( !file.open( QIODevice::WriteOnly ) )
            return 3;
        const auto bytes = QJsonDocument( results ).toJson( QJsonDocument::Indented );
        return file.write( bytes ) == bytes.size() ? 0 : 4;
    }
    klogg::vector<RegularExpressionPattern> allPatterns;
    for ( int i = 0; i < 6; ++i )
        allPatterns.emplace_back( QString::number( i ) );
    QJsonArray results;
    const auto record = [ & ]( const std::string& source, int count ) {
        klogg::vector<RegularExpressionPattern> patterns( allPatterns.begin(),
                                                          allPatterns.begin() + count );
        const auto expression = substitute( source, patterns );
        QElapsedTimer timer;
        timer.start();
        BooleanExpressionEvaluator evaluator( expression, patterns );
        const auto creationNs = timer.nsecsElapsed();
        std::string truth;
        if ( evaluator.isValid() ) {
            for ( unsigned combination = 0; combination < ( 1U << count ); ++combination ) {
                std::string variables( static_cast<size_t>( count ), 0 );
                for ( int i = 0; i < count; ++i )
                    variables[ static_cast<size_t>( i ) ]
                        = static_cast<char>( ( combination >> i ) & 1U );
                truth += evaluator.evaluate( variables ) ? '1' : '0';
            }
        }
        results.append( QJsonObject{ { "source", QString::fromStdString( source ) },
                                     { "patterns", count },
                                     { "valid", evaluator.isValid() },
                                     { "truth", QString::fromStdString( truth ) },
                                     { "error", QString::fromStdString( evaluator.errorString() ) },
                                     { "creation_ns", static_cast<double>( creationNs ) } } );
    };
    const std::vector<std::string> ops{ "and",  "nand", "or", "nor", "xor",
                                        "xnor", "&",    "|",  "&&",  "||" };
    for ( const auto& left : ops ) {
        record( "$0 " + left + " $1", 3 );
        for ( const auto& right : ops )
            record( "$0 " + left + " $1 " + right + " $2", 3 );
    }
    for ( const auto& expression : { "$0",
                                     "($0)",
                                     "not($0)",
                                     "!($0)",
                                     "!$0",
                                     "not $0",
                                     "!!$0",
                                     "not not $0",
                                     "not(not($0))",
                                     "!not($0)",
                                     "not(!$0)",
                                     "not($0) and $1",
                                     "$0 and not($1)",
                                     "$0($1)",
                                     "($0)($1)",
                                     "$0 $1",
                                     "($0) $1",
                                     "$0 not($1)",
                                     "not($0)not($1)",
                                     "$0\tand\t$1",
                                     "$0\nand\n$1",
                                     "$0\rand\r$1",
                                     "$0 AND $1",
                                     "$0and$1",
                                     "",
                                     "()",
                                     "not()",
                                     "!",
                                     "not",
                                     "($0",
                                     "$0)",
                                     "$0 and",
                                     "and $0",
                                     "$0 & & $1",
                                     "$0 ||| $1",
                                     "$0 &&& $1",
                                     "$0 ! $1",
                                     "$0 nor",
                                     "$0 or or $1",
                                     "$0 xor!($1)",
                                     "p_999999",
                                     "$0 + $1",
                                     "1 and $0",
                                     "$0; $1" } )
        record( expression, 3 );
    std::mt19937 random( 0x6b001U );
    for ( int i = 0; i < 256; ++i )
        record( randomExpression( random, 5 ), 6 );
    QFile file( app.arguments()[ 2 ] );
    if ( !file.open( QIODevice::WriteOnly ) )
        return 3;
    const auto bytes = QJsonDocument( results ).toJson( QJsonDocument::Indented );
    if ( file.write( bytes ) != bytes.size() )
        return 4;
    std::cout << "Wrote " << results.size() << " cases\n";
}
