#pragma once
#include "installationactivity_win.h"
#include "localchannel_win_p.h"
#include <string>

namespace zzlogg::updater::detail {
std::wstring quoteWindowsArgument(const std::wstring& value);
struct StartupProbeResult {
    bool passed=false;
    // False means a child could not be stopped: keep the transaction evidence,
    // never attempt to overwrite files it may still have loaded.
    bool processExited=true;
    DWORD error=0;
    DWORD exitCode=0;
    std::wstring detail;
};
StartupProbeResult runStartupProbe(const std::wstring& installRoot,DWORD timeoutMs=60000);
bool confirmApplicationStarted(HANDLE process,const DirectoryIdentity& identity,Deadline deadline);
}
