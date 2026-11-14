// Small code-generation comparison, not an alternative production type system.
#include "linetypes.h"

struct LocalLine {
    uint64_t value;
};
struct LocalCount {
    uint64_t value;
};

extern "C" uint64_t existing_advance( uint64_t line, uint64_t count )
{
    return ( LineNumber{ line } + LinesCount{ count } ).get();
}
extern "C" uint64_t local_advance( uint64_t line, uint64_t count )
{
    const LocalLine position{ line };
    const LocalCount amount{ count };
    const auto limit = std::numeric_limits<uint64_t>::max();
    return position.value <= limit - amount.value ? position.value + amount.value : limit;
}
extern "C" uint64_t existing_distance( uint64_t end, uint64_t begin )
{
    return ( LineNumber{ end } - LineNumber{ begin } ).get();
}
extern "C" uint64_t local_distance( uint64_t end, uint64_t begin )
{
    const LocalLine last{ end }, first{ begin };
    return last.value >= first.value ? last.value - first.value : 0;
}
extern "C" uint64_t existing_count_add( uint64_t first, uint64_t second )
{
    return ( LinesCount{ first } + LinesCount{ second } ).get();
}
extern "C" uint64_t local_count_add( uint64_t first, uint64_t second )
{
    return LocalCount{ first }.value + LocalCount{ second }.value;
}
