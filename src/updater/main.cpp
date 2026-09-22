#include "bootstrap_win_p.h"
#include "relay_p.h"
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
int wmain(int argc,wchar_t** argv){
    ChildBootstrap bootstrap;
    if(!bootstrap.open(argc,argv))return BootstrapRejected;
    // Production options: an empty launcher is the hardwired ShellExecuteEx
    // runas, and an elevated peer is required. There is no injectable launcher
    // here, and there never may be one.
    return runInstallRelay(bootstrap,CoordinatorOptions{});
}
