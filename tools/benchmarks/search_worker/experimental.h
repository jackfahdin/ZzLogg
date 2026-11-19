#pragma once
#include <cstdint>
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
struct Metrics {
    uint64_t matchers = 0, batches = 0, rows = 0, oversized = 0, maxRows = 0, maxBytes = 0;
    uint64_t acceleratedMatchers = 0;
    uint64_t programs = 0, jitPrograms = 0, fallbackPatterns = 0, codeBytes = 0, jitBytes = 0;
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
