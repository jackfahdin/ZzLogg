#pragma once
#include "searchcapacitygate.h"

namespace experimental {
// The probe and default worker share the same capacity notification algorithm.
using WaitStats = SearchCapacityWaitStats;
using WakeupGate = SearchCapacityGate;
}
