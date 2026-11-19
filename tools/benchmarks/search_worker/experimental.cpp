#include "experimental.h"
#include <atomic>
namespace experimental {
namespace { std::atomic<int64_t> blocks{0}, live{0}; }
BlockLifetime::BlockLifetime() { ++blocks; }
BlockLifetime::BlockLifetime(BlockLifetime&&) { ++blocks; }
BlockLifetime::~BlockLifetime() { --blocks; }
int64_t liveBlocks() { return blocks.load(); }
int64_t liveMatchers() { return live.load(); }
}
#include "matcher.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace experimental {
namespace {
using Patterns = klogg::vector<RegularExpressionPattern>;
std::atomic<uint64_t> programs{0}, jitPrograms{0}, fallbackPatterns{0}, codeBytes{0}, jitBytes{0};
std::atomic<uint64_t> direct{0}, fallback{0}, runtimeErrors{0}, peakMatchers{0};
std::atomic<bool> enabled{false}, diagnostics{false}, wakeup{false};
std::atomic<uint64_t> wakeSearches{0}, waits{0}, timeouts{0}, releases{0}, pollSleeps{0};
std::atomic<uint64_t> acceleratedCount{0};
std::atomic<uint64_t> matcherCount{0}, batchCount{0}, rowCount{0}, oversizedCount{0}, maxRows{0}, maxBytes{0};
void maximum(std::atomic<uint64_t>& value, uint64_t candidate)
{
    auto current = value.load();
    while (candidate > current && !value.compare_exchange_weak(current, candidate)) {}
}
}
void setWakeup(bool value) { wakeup = value; }
bool wakeupEnabled() { return wakeup.load(); }
void recordScheduling(bool wake, const WaitStats& stats, uint64_t sleeps)
{
    if (!diagnostics) return;
    if (wake) ++wakeSearches;
    waits += stats.waits; timeouts += stats.timeouts; releases += stats.releases; pollSleeps += sleeps;
}
void setEnabled(bool value) { enabled = value; }
void resetDiagnostics(bool value)
{
    if (liveMatchers() != 0 || liveBlocks() != 0) throw std::logic_error("Reset during active search");
    wakeSearches = 0; waits = 0; timeouts = 0; releases = 0; pollSleeps = 0;
    programs = 0; jitPrograms = 0; fallbackPatterns = 0; codeBytes = 0; jitBytes = 0;
    direct = 0; fallback = 0; runtimeErrors = 0; peakMatchers = 0;
    diagnostics = value;
    acceleratedCount = 0;
    matcherCount = 0; batchCount = 0; rowCount = 0; oversizedCount = 0; maxRows = 0; maxBytes = 0;
}
Metrics metrics() { return {matcherCount.load(), batchCount.load(), rowCount.load(),
                           oversizedCount.load(), maxRows.load(), maxBytes.load(), acceleratedCount.load(),
                           programs.load(), jitPrograms.load(), fallbackPatterns.load(), codeBytes.load(), jitBytes.load(),
                           wakeSearches.load(), waits.load(), timeouts.load(), releases.load(), pollSleeps.load(),
                           direct.load(), fallback.load(), runtimeErrors.load(), peakMatchers.load()}; }
void recordAcceleratedMatcher() { if (diagnostics) ++acceleratedCount; }
void recordOversized() { if (diagnostics) ++oversizedCount; }
size_t batchEnd(const klogg::vector<std::string_view>& lines, size_t start)
{
    if (start > lines.size()) throw std::out_of_range("Batch start out of range");
    size_t end = start, bytes = 0;
    while (end < lines.size() && end - start < MaxBatchRows
           && lines[end].size() <= MaxBatchBytes - bytes) {
        bytes += lines[end].size();
        ++end;
    }
    return end;
}
struct BatchMatcher::Impl {
#ifdef ZZLOGG_WORKER_PCRE2
    utf8regex::Matcher matcher;
    explicit Impl(const Patterns& patterns) : matcher(patterns) {}
#else
    std::unique_ptr<::LiteralMatcher> matcher;
    explicit Impl(const Patterns& patterns) : matcher(makeMatcher("memchr", patterns)) {}
#endif
};
BatchMatcher::BatchMatcher(const Patterns& patterns) : impl_(std::make_unique<Impl>(patterns))
{
    const auto current = ++live;
    if (diagnostics) {
        ++matcherCount;
        maximum(peakMatchers, static_cast<uint64_t>(current));
#ifdef ZZLOGG_WORKER_PCRE2
        const auto info = impl_->matcher.info();
        programs += info.programs; jitPrograms += info.jitPrograms; fallbackPatterns += info.fallbackPatterns;
        codeBytes += info.codeBytes; jitBytes += info.jitBytes;
#endif
    }
}
BatchMatcher::~BatchMatcher() { impl_.reset(); --live; }
std::string BatchMatcher::scan(const klogg::vector<std::string_view>& lines, size_t start, size_t count) const
{
    if (start > lines.size() || count == 0 || count > lines.size() - start
        || count > MaxBatchRows || count > batchEnd(lines, start) - start)
        throw std::out_of_range("Batch exceeds row or byte bound");
    const std::vector<std::string_view> batch(lines.begin() + static_cast<ptrdiff_t>(start),
                      lines.begin() + static_cast<ptrdiff_t>(start + count));
    if (diagnostics) {
        uint64_t bytes = 0;
        for (const auto line : batch) bytes += line.size();
        ++batchCount; rowCount += count;
        maximum(maxRows, count); maximum(maxBytes, bytes);
    }
#ifdef ZZLOGG_WORKER_PCRE2
    std::string bitmap;
    utf8regex::Routing routing;
    const bool profile = diagnostics.load();
    for (const auto line : batch) {
        const auto matched = impl_->matcher.match(line, profile ? &routing : nullptr);
        bitmap.append(matched.begin(), matched.end());
    }
    if (profile) {
        direct += routing.direct; fallback += routing.fallback; runtimeErrors += routing.runtimeErrors;
    }
    return bitmap;
#else
    return impl_->matcher->scan(batch);
#endif
}
std::unique_ptr<BatchMatcher> createMatcher(const Patterns& patterns)
{
    if (!enabled || patterns.empty()) return {};
    if (!usesPcre2 && !std::all_of(patterns.begin(), patterns.end(), [](const auto& p) {
            return p.isPlainText && p.isCaseSensitive && !p.pattern.isEmpty() && p.pattern.isValidUtf16();
        })) return {};
    return std::make_unique<BatchMatcher>(patterns);
}
}
