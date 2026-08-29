#include "applicationrunner.h"
#include "persistentinfo.h"

#ifdef KLOGG_PORTABLE
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

int main( int argc, char* argv[] )
{
    return runKloggApplication( argc, argv );
}
