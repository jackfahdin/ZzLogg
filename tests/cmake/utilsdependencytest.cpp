#include "active_screen.h"
#include "clipboard.h"
#include "containers.h"
#include "dispatch_to.h"
#include "openfilehelper.h"
#include "signalthrottler.h"

#if defined( QT_NETWORK_LIB ) || defined( QT_CONCURRENT_LIB )
#error "Utility consumers must not inherit unused Network or Concurrent modules"
#endif

int main()
{
    return 0;
}
