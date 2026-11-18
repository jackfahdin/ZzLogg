#include "experimental.h"
#include <atomic>
namespace experimental {
namespace { std::atomic<int64_t> blocks{0}; }
BlockLifetime::BlockLifetime() { ++blocks; }
BlockLifetime::BlockLifetime(BlockLifetime&&) { ++blocks; }
BlockLifetime::~BlockLifetime() { --blocks; }
int64_t liveBlocks() { return blocks.load(); }
}
#include "matcher.h"
#include <algorithm>
#include <stdexcept>
namespace experimental {
namespace {
std::atomic<bool> enabled{false}, diagnostics{false};
std::atomic<uint64_t> acceleratedCount{0};
std::atomic<uint64_t> matcherCount{0}, batchCount{0}, rowCount{0}, oversizedCount{0}, maxRows{0}, maxBytes{0};
void maximum(std::atomic<uint64_t>& value, uint64_t candidate)
{
    auto current = value.load();
    while (candidate > current && !value.compare_exchange_weak(current, candidate)) {}
}
}
void setEnabled(bool value) { enabled = value; }
void resetDiagnostics(bool value)
{
    diagnostics = value;
    acceleratedCount = 0;
    matcherCount = 0; batchCount = 0; rowCount = 0; oversizedCount = 0; maxRows = 0; maxBytes = 0;
}
Metrics metrics() { return {matcherCount.load(), batchCount.load(), rowCount.load(),
                           oversizedCount.load(), maxRows.load(), maxBytes.load(), acceleratedCount.load()}; }
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
    std::unique_ptr<::LiteralMatcher> matcher;
    explicit Impl(const Patterns& patterns) : matcher(makeMatcher("memchr", patterns)) {}
};
BatchMatcher::BatchMatcher(const Patterns& patterns) : impl_(std::make_unique<Impl>(patterns))
{
    if (diagnostics) ++matcherCount;
}
BatchMatcher::~BatchMatcher() = default;
std::string BatchMatcher::scan(const klogg::vector<std::string_view>& lines, size_t start, size_t count) const
{
    if (start > lines.size() || count == 0 || count > lines.size() - start
        || count > MaxBatchRows || count > batchEnd(lines, start) - start)
        throw std::out_of_range("Batch exceeds row or byte bound");
    const Lines batch(lines.begin() + static_cast<ptrdiff_t>(start),
                      lines.begin() + static_cast<ptrdiff_t>(start + count));
    if (diagnostics) {
        uint64_t bytes = 0;
        for (const auto line : batch) bytes += line.size();
        ++batchCount; rowCount += count;
        maximum(maxRows, count); maximum(maxBytes, bytes);
    }
    return impl_->matcher->scan(batch);
}
std::unique_ptr<BatchMatcher> createMatcher(const Patterns& patterns)
{
    if (!enabled || patterns.empty() || !std::all_of(patterns.begin(), patterns.end(), [](const auto& p) {
            return p.isPlainText && p.isCaseSensitive && !p.pattern.isEmpty() && p.pattern.isValidUtf16();
        })) return {};
    return std::make_unique<BatchMatcher>(patterns);
}
}
