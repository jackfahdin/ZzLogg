#pragma once
#include <cstdint>
#include "wakeup.h"
namespace experimental {
// Instantiated only in the instrumented worker copy, not in application objects.
struct BlockLifetime {
    BlockLifetime();
    BlockLifetime(BlockLifetime&&);
    ~BlockLifetime();
    BlockLifetime(const BlockLifetime&) = delete;
    BlockLifetime& operator=(const BlockLifetime&) = delete;
};
int64_t liveBlocks();
int64_t liveMatchers();
enum class RecoveryPoint { Connect, Read, Allocate, Match, Combine, BeforeDrain, BeforeNodeDestruction, ErrorReported };
using RecoveryHook = void (*)(RecoveryPoint);
void setRecoveryHook(RecoveryHook);
bool hasRecoveryHook();
void recoveryPoint(RecoveryPoint);
int64_t activeCallbacks();
struct CallbackLifetime {
    CallbackLifetime();
    ~CallbackLifetime();
    CallbackLifetime(const CallbackLifetime&) = delete;
    CallbackLifetime& operator=(const CallbackLifetime&) = delete;
};
#ifdef ZZLOGG_WORKER_PCRE2
inline constexpr bool usesPcre2 = true;
inline constexpr auto candidateName = "pcre2";
#else
inline constexpr bool usesPcre2 = false;
inline constexpr auto candidateName = "memchr";
#endif
}

#include "containers.h"
#include "regularexpressionpattern.h"
#include <memory>
#include <string>
#include <string_view>
namespace experimental {
constexpr size_t MaxBatchRows = 128;
constexpr size_t MaxBatchBytes = 256 * 1024;
void setEnabled(bool enabled); // Probe controller only, between completed searches.
void resetDiagnostics(bool enabled);
void setWakeup(bool enabled); // Only between fully joined searches.
bool wakeupEnabled();
void recordScheduling(bool wake, const WaitStats&, uint64_t pollSleeps);
struct Metrics {
    uint64_t matchers = 0, batches = 0, rows = 0, oversized = 0, maxRows = 0, maxBytes = 0;
    uint64_t acceleratedMatchers = 0;
    uint64_t programs = 0, jitPrograms = 0, fallbackPatterns = 0, codeBytes = 0, jitBytes = 0;
    uint64_t wakeSearches = 0, waits = 0, timeouts = 0, releases = 0, pollSleeps = 0;
    uint64_t direct = 0, fallback = 0, runtimeErrors = 0, peakMatchers = 0;
};
Metrics metrics();
size_t batchEnd(const klogg::vector<std::string_view>& lines, size_t start);
void recordOversized();
void recordAcceleratedMatcher();
class BatchMatcher {
public:
    explicit BatchMatcher(const klogg::vector<RegularExpressionPattern>&);
    ~BatchMatcher();
    std::string scan(const klogg::vector<std::string_view>&, size_t start, size_t count) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::unique_ptr<BatchMatcher> createMatcher(const klogg::vector<RegularExpressionPattern>&);
}
