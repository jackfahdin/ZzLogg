#include "linetypes.h"
#include <iostream>
#include <string_view>

template <typename T>
void printLayout( const char* name )
{
    std::cout << name << ": size=" << sizeof( T )
              << ", underlying=" << sizeof( typename T::UnderlyingType )
              << ", alignment=" << alignof( T ) << '\n';
}

int main( int argc, char** argv )
{
    if ( argc == 2 && std::string_view{ argv[ 1 ] } == "overflow" ) {
        // Volatile prevents constant folding: exercise the actual Release failure path.
        volatile int64_t value = int64_t{ std::numeric_limits<int>::max() } + 1;
        std::cout << type_safe::narrow_cast<int>( int64_t{ value } ) << '\n';
        return 1; // Reaching this line means the precondition did not reject overflow.
    }
    if ( argc != 1 )
        return 2;
    printLayout<OffsetInFile>( "OffsetInFile" );
    printLayout<LinesCount>( "LinesCount" );
    printLayout<LineNumber>( "LineNumber" );
    printLayout<LineLength>( "LineLength" );
    printLayout<LineColumn>( "LineColumn" );
    return 0;
}
