// Standalone, deterministic hot-matching benchmark; see README.md for limits.
#include "adapter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#ifdef __linux__
#include <dlfcn.h>
#endif

namespace {
struct Corpus {
    const char* name;
    int lineBytes;
    std::string filler;
};

std::vector<std::string> makeLines( const Corpus& corpus )
{
    std::vector<std::string> lines;
    for ( int index = 0; index < 128; ++index ) {
        std::string line;
        if ( corpus.lineBytes != 0 ) {
            const auto suffix = " needle" + std::to_string( index % 16 ) + " ";
            const auto limit = static_cast<size_t>( corpus.lineBytes ) - suffix.size();
            while ( line.size() + corpus.filler.size() <= limit ) line += corpus.filler;
            line.append( limit - line.size(), ' ' );
            line += suffix;
        }
        lines.push_back( std::move( line ) );
    }
    return lines;
}

uint64_t scan( const BenchmarkMatcher& matcher,
               const std::vector<std::string>& lines, int passes )
{
    uint64_t checksum = 0;
    for ( int pass = 0; pass < passes; ++pass ) {
        for ( const auto& line : lines ) {
            const auto result = matcher.match( line );
            for ( size_t i = 0; i < result.size(); ++i ) {
                checksum += static_cast<unsigned char>( result[i] ) * ( i + 1 );
            }
        }
    }
    return checksum;
}
} // namespace

int main( int argc, char** argv )
{
    QCoreApplication app( argc, argv );
    const bool allocations = app.arguments().contains( "--allocations" );
    const bool reverse = app.arguments().contains( "--reverse" );
    using Toggle = void ( * )();
    using Counter = uint64_t ( * )();
    Toggle begin = nullptr, end = nullptr;
    Counter calls = nullptr, bytes = nullptr;
#ifdef __linux__
    begin = reinterpret_cast<Toggle>( dlsym( RTLD_DEFAULT, "regex_alloc_begin" ) );
    end = reinterpret_cast<Toggle>( dlsym( RTLD_DEFAULT, "regex_alloc_end" ) );
    calls = reinterpret_cast<Counter>( dlsym( RTLD_DEFAULT, "regex_alloc_calls" ) );
    bytes = reinterpret_cast<Counter>( dlsym( RTLD_DEFAULT, "regex_alloc_bytes" ) );
#endif
    if ( allocations && ( !begin || !end || !calls || !bytes ) ) {
        std::cerr << "Allocation mode requires the glibc LD_PRELOAD probe\n";
        return 2;
    }
    const std::vector<Corpus> corpora{
        { "short_ascii", 256, "INFO abcdef " },
        { "short_unicode", 256, "日志中文😀 " },
        { "long_ascii", 8192, "INFO abcdef " },
        { "long_unicode", 8192, "日志中文😀 " },
        { "invalid_utf8", 256, std::string{ "\xFF\xC0\xAF\xE4\xB8" } },
        { "embedded_nul", 256, std::string{ "abc\0def ", 8 } },
        { "empty", 0, "" }
    };
    for ( const auto& corpus : corpora ) {
        const auto lines = makeLines( corpus );
        QCryptographicHash inputHash( QCryptographicHash::Sha256 );
        for ( const auto& line : lines ) inputHash.addData( QByteArrayView( line.data(), line.size() ) );
        for ( int count : { 0, 1, 4, 16 } ) {
            klogg::vector<RegularExpressionPattern> patterns;
            std::vector<QRegularExpression> reference;
            for ( int i = 0; i < count; ++i ) {
                patterns.emplace_back( QStringLiteral( "\\bneedle%1\\b" ).arg( i ) );
                reference.push_back( static_cast<QRegularExpression>( patterns.back() ) );
            }
            std::vector<std::pair<const char*, std::unique_ptr<BenchmarkMatcher>>> matchers;
            if ( reverse ) {
                matchers.emplace_back( "current", makeCurrent( patterns ) );
                matchers.emplace_back( "baseline", makeBaseline( patterns ) );
            }
            else {
                matchers.emplace_back( "baseline", makeBaseline( patterns ) );
                matchers.emplace_back( "current", makeCurrent( patterns ) );
            }
            std::array<QJsonObject, 2> metadata;
            std::array<uint64_t, 2> expectedChecksums;
            std::array<int, 2> passes;
            // Compile, verify and warm BOTH instances before timing either one.
            for ( size_t index = 0; index < matchers.size(); ++index ) {
                const auto& [version, instance] = matchers[index];
                const auto& matcher = *instance;
                QCryptographicHash resultHash( QCryptographicHash::Sha256 );
                for ( const auto& line : lines ) {
                    std::string expected;
                    for ( const auto& regexp : reference ) {
                        expected.push_back( static_cast<char>( regexp.match(
                            QString::fromUtf8( line.data(), klogg::isize( line ) ) ).hasMatch() ) );
                    }
                    const auto actual = matcher.match( line );
                    if ( actual != expected ) throw std::runtime_error( "Matching semantics changed" );
                    resultHash.addData( QByteArrayView( actual.data(), actual.size() ) );
                }
                QElapsedTimer warmup;
                warmup.start();
                expectedChecksums[index] = scan( matcher, lines, 2 ) / 2;
                const auto warmupNs = std::max<qint64>( warmup.nsecsElapsed(), 1 );
                passes[index] = allocations ? 1
                    : static_cast<int>( std::clamp<qint64>( 60000000 / warmupNs, 4, 65536 ) );
                metadata[index] = QJsonObject{
                    { "version", version },
                    { "corpus", corpus.name }, { "line_bytes", corpus.lineBytes },
                    { "patterns", count },
                    { "input_sha256", QString::fromLatin1( inputHash.result().toHex() ) },
                    { "result_sha256", QString::fromLatin1( resultHash.result().toHex() ) },
                    { "checksum_per_pass", static_cast<double>( expectedChecksums[index] ) },
                    { "qt", qVersion() }
                };
            }
            // Reuse the same warmed pair in ABBA order to balance first/second effects.
            const std::vector<size_t> order = allocations ? std::vector<size_t>{ 0, 1 }
                                                          : std::vector<size_t>{ 0, 1, 1, 0 };
            for ( size_t index : order ) {
                const auto& matcher = *matchers[index].second;
                QElapsedTimer timer;
                if ( allocations ) begin();
                timer.start();
                const auto checksum = scan( matcher, lines, passes[index] );
                const auto elapsed = timer.nsecsElapsed();
                if ( allocations ) end();
                if ( checksum != expectedChecksums[index] * static_cast<uint64_t>( passes[index] ) ) {
                    throw std::runtime_error( "Result checksum changed" );
                }
                const double totalLines = static_cast<double>( lines.size() ) * passes[index];
                auto row = metadata[index];
                row.insert( "lines", totalLines );
                row.insert( "ns_per_line", elapsed / totalLines );
                if ( allocations ) {
                    row.insert( "allocations_per_line", calls() / totalLines );
                    row.insert( "requested_bytes_per_line", bytes() / totalLines );
                }
                std::cout << QJsonDocument( row ).toJson( QJsonDocument::Compact ).constData() << '\n';
            }
        }
    }
}
