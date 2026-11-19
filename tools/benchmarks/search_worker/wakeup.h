#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace experimental {
struct WaitStats { uint64_t waits = 0, timeouts = 0, releases = 0; };
// One producer, serialized capacity checks/decrements. Outlives the search graph.
class WakeupGate {
public:
    explicit WakeupGate(std::chrono::milliseconds interval = std::chrono::milliseconds(1)) : interval_(interval) {}
    template<class TryPut, class Cancelled>
    bool submit(TryPut tryPut, Cancelled cancelled)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!cancelled()) {
            if (tryPut()) return true;
            ++stats_.waits;
            const auto released = stats_.releases;
            if (!available_.wait_for(lock, interval_, [&] { return stats_.releases != released; }))
                ++stats_.timeouts;
        }
        return false;
    }
    template<class Decrement>
    void release(Decrement decrement)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            decrement();
            ++stats_.releases;
        }
        available_.notify_one();
    }
    WaitStats stats() const { std::lock_guard<std::mutex> lock(mutex_); return stats_; }
private:
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::chrono::milliseconds interval_;
    WaitStats stats_;
};
}
