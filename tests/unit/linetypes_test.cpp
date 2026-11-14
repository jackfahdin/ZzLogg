#include <catch2/catch.hpp>

#include "linetypes.h"

#include <QVariant>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {
template <typename T, typename U, typename = void>
struct CanAdd : std::false_type {};
template <typename T, typename U>
struct CanAdd<T, U, std::void_t<decltype( std::declval<T>() + std::declval<U>() )>>
    : std::true_type {};
template <typename T, typename U, typename = void>
struct CanSubtract : std::false_type {};
template <typename T, typename U>
struct CanSubtract<T, U, std::void_t<decltype( std::declval<T>() - std::declval<U>() )>>
    : std::true_type {};
template <typename T, typename U, typename = void>
struct CanEqual : std::false_type {};
template <typename T, typename U>
struct CanEqual<T, U, std::void_t<decltype( std::declval<T>() == std::declval<U>() )>>
    : std::true_type {};

using Units = std::tuple<OffsetInFile, LinesCount, LineNumber, LineLength, LineColumn>;

template <typename T, typename U>
constexpr bool isolatedUnits()
{
    if constexpr ( !std::is_same_v<T, U> ) {
        static_assert( !std::is_convertible_v<T, U> );
        static_assert( !std::is_constructible_v<T, U> );
        static_assert( !std::is_assignable_v<T&, U> );
        static_assert( !CanEqual<T, U>::value );
        constexpr bool meaningful
            = (std::is_same_v<T, LineNumber> && std::is_same_v<U, LinesCount>)
              || ( std::is_same_v<T, LineColumn> && std::is_same_v<U, LineLength> );
        static_assert( CanAdd<T, U>::value == meaningful );
        static_assert( CanSubtract<T, U>::value == meaningful );
    }
    return true;
}

template <typename T, size_t... I>
constexpr bool checkUnit( std::index_sequence<I...> )
{
    using Value = typename T::UnderlyingType;
    static_assert( std::is_trivially_copyable_v<T> );
    static_assert( std::is_constructible_v<T, Value> );
    static_assert( !std::is_convertible_v<Value, T> );
    static_assert( !std::is_convertible_v<T, Value> );
    static_assert( !CanAdd<T, Value>::value );
    static_assert( !CanSubtract<T, Value>::value );
    return ( isolatedUnits<T, std::tuple_element_t<I, Units>>() && ... );
}

template <size_t... I>
constexpr bool checkUnits( std::index_sequence<I...> indices )
{
    return ( checkUnit<std::tuple_element_t<I, Units>>( indices ) && ... );
}
static_assert( checkUnits( std::make_index_sequence<std::tuple_size_v<Units>>{} ) );
static_assert( !CanAdd<LineNumber, LineNumber>::value );
static_assert( !CanAdd<LineColumn, LineColumn>::value );
static_assert( std::is_same_v<decltype( 1_lnum + 1_lcount ), LineNumber> );
static_assert( std::is_same_v<decltype( 1_lnum - 1_lnum ), LinesCount> );
static_assert( std::is_same_v<decltype( 1_lcol + 1_length ), LineColumn> );
static_assert( std::is_same_v<decltype( 1_lcol - 1_lcol ), LineLength> );

template <typename T>
void checkValue()
{
    REQUIRE( T{}.get() == 0 );
    const T original{ 17 };
    const auto copy = original;
    REQUIRE( copy.get() == 17 );
    REQUIRE( copy == original );
    REQUIRE( T{ 16 } < copy );
    REQUIRE( copy <= T{ 17 } );
    REQUIRE( copy > T{ 16 } );
    REQUIRE( copy >= T{ 17 } );
    REQUIRE( copy != T{ 18 } );
    QString rendered;
    {
        QDebug( &rendered ).nospace() << original;
    }
    REQUIRE( rendered == QStringLiteral( "17" ) );
}

template <typename T>
void checkMetaType()
{
    const auto variant = QVariant::fromValue( T{ 23 } );
    REQUIRE( variant.metaType() == QMetaType::fromType<T>() );
    REQUIRE( variant.template value<T>() == T{ 23 } );
}
} // namespace

TEST_CASE( "Log units preserve explicit values, Qt metadata and sentinel positions", "[linetypes]" )
{
    checkValue<OffsetInFile>();
    checkValue<LinesCount>();
    checkValue<LineNumber>();
    checkValue<LineLength>();
    checkValue<LineColumn>();
    checkMetaType<LinesCount>();
    checkMetaType<LineNumber>();
    checkMetaType<LineLength>();
    checkMetaType<LineColumn>();
    REQUIRE( OffsetInFile{ -1 }.get() == -1 );
    REQUIRE( FilePosition{}.line() == 0_lnum );
    REQUIRE( FilePosition{}.column() == LineColumn{ -1 } );
    REQUIRE( FilePosition{ 2_lnum, 3_lcol } == FilePosition{ 2_lnum, 3_lcol } );
    REQUIRE_FALSE( FilePosition{ 2_lnum, 3_lcol } == FilePosition{ 2_lnum, 4_lcol } );
    OptionalLineNumber missing;
    REQUIRE_FALSE( missing.has_value() );
    missing = 0_lnum;
    REQUIRE( missing.has_value() );
    REQUIRE( *missing == 0_lnum );
}

TEST_CASE( "Log unit operations retain saturation and their result units", "[linetypes]" )
{
    const auto maxLine = maxValue<LineNumber>();
    REQUIRE( maxLine + 1_lcount == maxLine );
    REQUIRE( ( maxLine - 1_lcount ) + 2_lcount == maxLine );
    REQUIRE( 0_lnum - 1_lcount == 0_lnum );
    REQUIRE( 3_lnum - 5_lnum == 0_lcount );
    REQUIRE( 5_lnum - 3_lnum == 2_lcount );
    auto line = 2_lnum;
    line += 3_lcount;
    REQUIRE( line == 5_lnum );
    REQUIRE( line++ == 5_lnum );
    REQUIRE( ++line == 7_lnum );
    REQUIRE( line-- == 7_lnum );
    REQUIRE( --line == 5_lnum );
    REQUIRE( 4_lnum < 5_lcount );
    REQUIRE( 5_lnum >= 5_lcount );

    const auto maxColumn = maxValue<LineColumn>();
    REQUIRE( maxColumn + 1_length == maxColumn );
    REQUIRE( ( maxColumn - 1_length ) + 2_length == maxColumn );
    REQUIRE( 0_lcol - 1_length == 0_lcol );
    REQUIRE( 3_lcol - 5_lcol == 0_length );
    REQUIRE( 5_lcol - 3_lcol == 2_length );
    auto column = 2_lcol;
    column += 3_length;
    REQUIRE( column == 5_lcol );
    REQUIRE( column++ == 5_lcol );
    REQUIRE( ++column == 7_lcol );
    REQUIRE( OffsetInFile{ -1 } + 2_offset == 1_offset );
    REQUIRE( 3_offset - 2_offset == 1_offset );
    auto offset = 1_offset;
    REQUIRE( offset++ == 1_offset );
    REQUIRE( ++offset == 3_offset );
    offset += 2_offset;
    REQUIRE( offset == 5_offset );
    offset -= 1_offset;
    REQUIRE( offset == 4_offset );
    auto count = 2_lcount;
    REQUIRE( count++ == 2_lcount );
    REQUIRE( ++count == 4_lcount );
    REQUIRE( count-- == 4_lcount );
    REQUIRE( --count == 2_lcount );
    REQUIRE( 2_lcount + 3_lcount == 5_lcount );
    REQUIRE( 5_lcount - 3_lcount == 2_lcount );
    REQUIRE( 2_length + 3_length == 5_length );
    REQUIRE( 5_length - 3_length == 2_length );
    // Saturation belongs to position arithmetic, not all underlying operations.
    REQUIRE( maxValue<LinesCount>() + 1_lcount == 0_lcount );
}

TEST_CASE( "Narrowing preserves representable integer boundaries", "[linetypes]" )
{
    const auto intMin = std::numeric_limits<int>::min();
    const auto intMax = std::numeric_limits<int>::max();
    REQUIRE( type_safe::narrow_cast<int>( int64_t{ intMin } ) == intMin );
    REQUIRE( type_safe::narrow_cast<int>( int64_t{ intMax } ) == intMax );
    REQUIRE( type_safe::narrow_cast<uint32_t>( uint64_t{ 0 } ) == 0 );
    REQUIRE( type_safe::narrow_cast<uint32_t>( uint64_t{ UINT32_MAX } ) == UINT32_MAX );
    REQUIRE( OffsetInFile{ intMax }.get<int>() == intMax );
    REQUIRE( OffsetInFile{ 17 }.get<size_t>() == 17 );
    REQUIRE( LinesCount{ UINT32_MAX }.get<uint32_t>() == UINT32_MAX );
    REQUIRE( LineLength{ intMax }.get<int>() == intMax );
    REQUIRE( LineColumn{ 17 }.get<size_t>() == 17 );
    REQUIRE( klogg::isize( std::string_view{} ) == 0 );
    REQUIRE( klogg::isize( std::string_view{ "abc" } ) == 3 );
}
