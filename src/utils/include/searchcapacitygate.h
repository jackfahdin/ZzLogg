#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

struct SearchCapacityWaitStats {
    uint64_t waits = 0;
    uint64_t timeouts = 0;
    uint64_t releases = 0;
};

// One producer. Capacity checks and decrements are serialized by the same lock,
// so a release between rejection and sleep cannot be lost. Callbacks must not
// re-enter this gate or execute graph tasks inline; they only submit/decrement.
// The owner must drain the graph while this gate and all callback captures live.
class SearchCapacityGate {
public:
    explicit SearchCapacityGate( std::chrono::milliseconds interval = std::chrono::milliseconds( 1 ) )
        : interval_( interval )
    {
    }

    template <class TryPut, class Cancelled>
    bool submit( TryPut tryPut, Cancelled cancelled )
    {
        std::unique_lock<std::mutex> lock( mutex_ );
        while ( !cancelled() ) {
            if ( tryPut() ) return true;
            ++stats_.waits;
            const auto released = stats_.releases;
            // Even with no completed block, periodically observe cancellation or
            // loss of background TBB slots. Single-slot callers must drive the graph.
            if ( !available_.wait_for( lock, interval_, [ & ] { return stats_.releases != released; } ) )
                ++stats_.timeouts;
        }
        return false;
    }

    template <class Decrement>
    void release( Decrement decrement )
    {
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            decrement();
            ++stats_.releases;
        }
        available_.notify_one();
    }

    SearchCapacityWaitStats stats() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return stats_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::chrono::milliseconds interval_;
    SearchCapacityWaitStats stats_;
};
