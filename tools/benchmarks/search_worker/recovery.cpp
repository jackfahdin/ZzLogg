#include "harness.h"
#include "configuration.h"
#include <QEventLoop>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <tbb/info.h>

namespace workerprobe {
namespace {
struct Injection {
    QString mode;
    bool concurrentProducer = tbb::info::default_concurrency() > 1;
    std::atomic<bool> armed{false};
    std::atomic<unsigned> connections{0}, reads{0}, allocations{0}, matches{0}, combines{0}, errors{0}, drains{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool callbackEntered = false, release = false, visible = false;
};
Injection* injection = nullptr; // Installed before search; removed only after join.
void checkpoint(experimental::RecoveryPoint point)
{
    using Point = experimental::RecoveryPoint;
    auto& state = *injection;
    if (point == Point::BeforeNodeDestruction) {
        if (state.connections > 0 && state.drains == 0) {
            std::cerr << "Node destruction before draining scheduled edge tasks\n";
            std::_Exit(71);
        }
        if (experimental::activeCallbacks() != 0) {
            std::cerr << "Node destruction started with an active callback\n";
            std::_Exit(70); // Fail the isolated child before a deliberate UAF can occur.
        }
        return;
    }
    if (point == Point::BeforeDrain) {
        ++state.drains;
        { std::lock_guard<std::mutex> lock(state.mutex); state.release = true; }
        state.changed.notify_all();
        return;
    }
    if (point == Point::ErrorReported) { ++state.errors; return; }
    if (!state.armed) return;
    if (point == Point::Connect && state.mode == "connect") {
        ++state.connections;
        state.armed = false;
        throw std::bad_alloc();
    }
    const bool producer = state.mode == "read" || state.mode == "allocation";
    if (point == Point::Match) {
        if (++state.matches == 1 && producer && state.concurrentProducer) {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.callbackEntered = true;
            state.changed.notify_all();
            require(state.changed.wait_for(lock, std::chrono::seconds(5), [&] { return state.release; }),
                    "Producer did not enter exception drain");
        } else if (state.mode == "match" && state.armed.exchange(false)) throw std::bad_alloc();
    }
    if (point == Point::Combine && ++state.combines == 2 && state.mode == "combine") {
        std::unique_lock<std::mutex> lock(state.mutex);
        require(state.changed.wait_for(lock, std::chrono::seconds(5), [&] { return state.visible; }),
                "No partial result delivered before injected combine failure");
        state.armed = false;
        throw std::runtime_error("injected result failure");
    }
    if (point == Point::Read && state.mode == "completion" && state.armed.exchange(false))
        throw std::runtime_error("injected initial read failure");
    if ((point == Point::Read && ++state.reads == 4 && state.mode == "read")
        || (point == Point::Allocate && ++state.allocations == 4 && state.mode == "allocation")) {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (state.concurrentProducer) {
            require(state.changed.wait_for(lock, std::chrono::seconds(5), [&] { return state.callbackEntered; }),
                    "No active callback at producer failure");
        }
        state.armed = false;
        if (state.mode == "allocation") throw std::bad_alloc();
        throw std::runtime_error("injected file read failure");
    }
}
}
void verifyRecovery(const QString& mode, bool wake)
{
    require(mode == "connect" || mode == "read" || mode == "allocation" || mode == "match" || mode == "combine"
                || mode == "completion", "Unknown failure contract");
    constexpr uint64_t rows = 8192;
    QByteArray bytes;
    for (uint64_t row = 0; row < rows; ++row) bytes += "ERROR\trow\n";
    Configuration::get().setUseParallelSearch(false);
    Configuration::get().setSearchThreadPoolSize(1);
    Configuration::get().setSearchReadBufferSizeLines(128);
    experimental::setEnabled(false);
    experimental::setWakeup(wake);
    for (bool update : {false, true}) {
        Fixture fixture(bytes);
        uint64_t expectedRows = rows;
        Injection state;
        state.mode = mode;
        injection = &state;
        experimental::setRecoveryHook(checkpoint);
        auto worker = std::make_unique<LogFilteredDataWorker>(fixture.log);
        SearchResultArray visible;
        const auto run = [&](bool updating, bool failure) {
            QEventLoop loop;
            QTimer deadline;
            deadline.setSingleShot(true);
            bool expired = false;
            unsigned finished = 0, terminal = 0;
            const auto progress = QObject::connect(worker.get(), &LogFilteredDataWorker::searchProgressed, &loop,
                [&](LinesCount, int percent, LineNumber) {
                    const auto result = worker->getSearchResults();
                    visible -= result.removedMatches;
                    visible |= result.newMatches;
                    if (!visible.isEmpty()) {
                        { std::lock_guard<std::mutex> lock(state.mutex); state.visible = true; }
                        state.changed.notify_all();
                    }
                    if (percent == 100) ++terminal;
                });
            const auto done = QObject::connect(worker.get(), &LogFilteredDataWorker::searchFinished, &loop,
                [&] { ++finished; loop.quit(); });
            QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
                expired = true; worker->interrupt(); loop.quit();
            });
            deadline.start(7000);
            if (updating) worker->updateSearch({"ERROR", true, false, false, true}, 0_lnum, LineNumber{expectedRows}, 0_lnum);
            else worker->search({"ERROR", true, false, false, true}, 0_lnum, LineNumber{expectedRows});
            if (!finished) loop.exec();
            deadline.stop();
            QObject::disconnect(progress); QObject::disconnect(done);
            require(!expired && finished == 1 && terminal == 1, "Failure/search did not deliver one terminal and completion");
            const auto result = worker->getSearchResults();
            require(failure ? visible.isEmpty() && result.processedLines.get() == 0
                            : visible.cardinality() == expectedRows && result.processedLines.get() == expectedRows,
                    "Failure retained partial/stale results or recovery lost rows");
        };
        if (update) {
            run(false, false);
            require(fixture.file.seek(fixture.file.size()) && fixture.file.write(bytes) == bytes.size()
                        && fixture.file.flush(), "Cannot append recovery fixture");
            fixture.reload();
            expectedRows += rows;
        }
        state.armed = true;
        run(update, true);
        // This call also joins the prior operation before starting another on the same worker.
        run(false, false);
        worker.reset();
        require(state.errors == 1 && state.drains == 1, "Failure was not reported/drained exactly once");
        require(experimental::activeCallbacks() == 0 && experimental::liveBlocks() == 0
                    && experimental::liveMatchers() == 0, "Failed/repeated search leaked graph state");
        experimental::setRecoveryHook(nullptr);
        injection = nullptr;
    }
    std::cout << "Worker failure/research contract passed: " << mode.toStdString()
              << (wake ? " wake" : " poll") << '\n';
}
}
