#include "searchcapacitygate.h"
#include <algorithm>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void verifyWakeup()
{
    using namespace std::chrono_literals;
    // A release between failed check and actual sleep must not be lost.
    SearchCapacityGate gate(5s);
    bool capacity = false; // Accessed only inside gate callbacks, under the same mutex.
    std::promise<void> rejected;
    auto ready = rejected.get_future();
    bool first = true;
    auto producer = std::async(std::launch::async, [&] {
        return gate.submit([&] {
            if (first) { first = false; rejected.set_value(); }
            return capacity;
        }, [] { return false; });
    });
    ready.wait();
    gate.release([&] { capacity = true; });
    // A predicate can become true only at the fallback deadline even without a
    // notification. Check readiness before that deadline, not just wait statistics.
    require(producer.wait_for(2s) == std::future_status::ready,
            "Capacity release did not wake producer before fallback");
    require(producer.get(), "Capacity release did not admit producer");
    require(gate.stats().waits == 1 && gate.stats().timeouts == 0,
            "Capacity notification was lost; producer waited for polling timeout");
    SearchCapacityGate early;
    bool free = false;
    early.release([&] { free = true; });
    require(early.submit([&] { return free; }, [] { return false; }) && early.stats().waits == 0,
            "Release before submit was lost");
    SearchCapacityGate cancelled;
    std::atomic<bool> stop{false};
    std::promise<void> blocked;
    auto waiting = blocked.get_future();
    bool announced = false;
    auto pending = std::async(std::launch::async, [&] {
        return cancelled.submit([&] {
            if (!announced) { announced = true; blocked.set_value(); }
            return false;
        }, [&] { return stop.load(); });
    });
    waiting.wait();
    stop = true; // No release/notification: bounded timeout must still observe cancellation.
    require(pending.wait_for(2s) == std::future_status::ready && !pending.get(),
            "Cancelled producer remained blocked without a capacity release");
    require(cancelled.stats().timeouts > 0, "Cancellation fallback was not exercised");
    SearchCapacityGate bounded;
    std::atomic<int> outstanding{0};
    constexpr int limit = 3, count = 4096;
    int maximum = 0;
    auto consumer = std::async(std::launch::async, [&] {
        for (int done = 0; done < count;) {
            if (outstanding.load() == 0) { std::this_thread::yield(); continue; }
            bounded.release([&] { --outstanding; });
            ++done;
        }
    });
    for (int row = 0; row < count; ++row) {
        require(bounded.submit([&] {
            if (outstanding.load() == limit) return false;
            maximum = std::max(maximum, ++outstanding);
            return true;
        }, [] { return false; }), "Uncancelled bounded submission failed");
    }
    consumer.get();
    require(maximum <= limit && outstanding == 0 && bounded.stats().releases == count,
            "Notifications changed capacity or lost/duplicated a release");
}
}
int main()
{
    try {
        verifyWakeup();
        std::cout << "Capacity notification, cancellation and bounded submission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
