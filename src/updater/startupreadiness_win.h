#pragma once
#include "processidentity_win_p.h"
namespace zzlogg::updater::detail {
// The process handle held by the updater prevents PID reuse during observation.
// This event is published only after real storage/UI initialization and one
// main-event-loop dispatch, independently of the early single-instance pipe.
inline std::wstring startupReadyEventName(DWORD pid) {
    return L"Local\\ZzLogg.StartupReady."+std::to_wstring(pid);
}
}
