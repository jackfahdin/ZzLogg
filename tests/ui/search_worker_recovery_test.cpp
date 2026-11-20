#include "configuration.h"
#include "logdata.h"
#include "logfiltereddataworker.h"
#include "storagecontext.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <tbb/global_control.h>
#include <tbb/task_arena.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr uint64_t Rows = 8192;
void configure(unsigned threads)
{
    Configuration::get().setUseParallelSearch(threads > 1);
    Configuration::get().setSearchThreadPoolSize(static_cast<int>(threads));
    Configuration::get().setSearchReadBufferSizeLines(128);
}

void checkResults(const SearchResults& result, LinesCount reported, bool odd, bool complete)
{
    const auto count = result.newMatches.cardinality();
    require(result.removedMatches.isEmpty(), "Full search retained removals");
    require(reported.get() == count, "Terminal count differs from delivered matches");
    require(result.processedLines.get() <= Rows, "Processed extent exceeds fixture");
    require(result.maxLength.get() == (count ? (odd ? 2U : 11U) : 0U),
            "Search retained wrong tab-expanded width");
    for (auto row : result.newMatches) {
        require(row < result.processedLines.get() && row % 2 == (odd ? 1U : 0U),
                "Wrong matched row or processed extent");
    }
    if (complete) {
        require(result.processedLines.get() == Rows && count == Rows / 2,
                "Complete result lost rows");
    }
}

void checkConsumed(const SearchResults& after, const SearchResults& before)
{
    require(after.newMatches.isEmpty() && after.removedMatches.isEmpty(),
            "Consumed result was delivered twice");
    require(after.maxLength == before.maxLength && after.processedLines == before.processedLines,
            "Consuming results changed search metadata");
}

void runWorker(LogFilteredDataWorker& worker, bool cancel, bool odd = false)
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool expired = false, requested = false, wrongOrder = false;
    unsigned finished = 0, terminal = 0;
    LinesCount reported{0};
    QObject::connect(&worker, &LogFilteredDataWorker::searchProgressed, &loop,
        [&](LinesCount count, int percent, LineNumber) {
            if (finished || terminal) wrongOrder = true;
            if (percent == 100) { ++terminal; reported = count; }
            if (cancel && percent < 100 && !requested) { requested = true; worker.interrupt(); }
        });
    QObject::connect(&worker, &LogFilteredDataWorker::searchFinished, &loop,
        [&] { if (terminal != 1) wrongOrder = true; ++finished; loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop,
        [&] { expired = true; worker.interrupt(); loop.quit(); });
    deadline.start(4000);
    worker.search({odd ? "ok" : "ERROR", true, false, false, true}, 0_lnum, LineNumber{Rows});
    if (!finished) loop.exec();
    deadline.stop();
    // Include already-posted duplicate terminal/completion relays in this run's checks.
    QCoreApplication::sendPostedEvents(&worker, QEvent::MetaCall);
    require(!expired, "Single-slot search made no progress before deadline");
    require(!wrongOrder && finished == 1 && terminal == 1, "Search did not finish exactly once in order");
    const auto result = worker.getSearchResults();
    require(!cancel || requested, "Cancellation was not requested");
    // The queued progress relay can arrive after the producer has completed.
    // Partial cancellation is asserted deterministically in runArena instead.
    checkResults(result, reported, odd, !cancel);
    checkConsumed(worker.getSearchResults(), result);
}

void runArena(LogData& log, unsigned threads)
{
    configure(threads);
    AtomicFlag cancelled;
    SearchData data;
    FullSearchOperation operation(log, cancelled, {"ERROR", true, false, false, true}, 0_lnum, LineNumber{Rows});
    bool cancel = false, requested = false, wrongOrder = false;
    unsigned finished = 0, terminal = 0;
    LinesCount reported{0};
    // arena(1) executes callbacks on this calling thread. Cancellation therefore
    // happens before the next result is combined, without a queued UI relay.
    QObject::connect(&operation, &SearchOperation::searchProgressed, &operation,
        [&](LinesCount count, int percent, LineNumber) {
            if (finished || terminal) wrongOrder = true;
            if (percent == 100) { ++terminal; reported = count; }
            else if (cancel && !requested) { requested = true; cancelled.set(); }
        }, Qt::DirectConnection);
    QObject::connect(&operation, &SearchOperation::searchFinished, &operation,
        [&] { if (terminal != 1) wrongOrder = true; ++finished; }, Qt::DirectConnection);
    for (bool shouldCancel : {false, true, false}) {
        cancel = shouldCancel;
        requested = wrongOrder = false;
        finished = terminal = 0;
        reported = LinesCount{0};
        cancelled.clear();
        operation.run(data);
        require(!wrongOrder && finished == 1 && terminal == 1,
                "One-slot arena did not finish exactly once in order");
        const auto result = data.takeCurrentResults();
        checkResults(result, reported, false, !cancel);
        require(data.getNbMatches() == reported, "One-slot arena retained old match count");
        if (cancel) {
            require(requested && result.processedLines.get() > 0 && result.processedLines.get() < Rows
                        && result.newMatches.cardinality() > 0 && result.newMatches.cardinality() < Rows / 2,
                    "One-slot arena cancellation missed active search");
        }
        checkConsumed(data.takeCurrentResults(), result);
    }
}

}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    if (!root.isValid() || !StorageContext::install({StorageMode::CustomDirectory, root.path(),
        root.filePath("settings.ini"), true})) return 2;
    qRegisterMetaType<LoadingStatus>(); qRegisterMetaType<LinesCount>(); qRegisterMetaType<LineNumber>();
    try {
        Configuration::getSynced().setRegexpEnging(RegexpEngine::QRegularExpression);
        QTemporaryFile file;
        require(file.open(), "Cannot open fixture");
        for (uint64_t row = 0; row < Rows; ++row) file.write(row % 2 == 0 ? "ERROR\trow\n" : "ok\n");
        require(file.flush(), "Cannot flush fixture");
        LogData log;
        QSignalSpy loaded(&log, &LogData::loadingFinished);
        log.attachFile(file.fileName());
        require(!loaded.isEmpty() || loaded.wait(10000), "Indexing timeout");
        require(qvariant_cast<LoadingStatus>(loaded.last().at(0)) == LoadingStatus::Successful, "Index failed");
        Configuration::get().setRegexpEnging(RegexpEngine::QRegularExpression);
        {
            tbb::global_control limit(tbb::global_control::max_allowed_parallelism, 1);
            for (unsigned threads : {1U, 2U}) {
                configure(threads);
                LogFilteredDataWorker worker(log);
                runWorker(worker, false);
                runWorker(worker, true);
                runWorker(worker, false, true);
                runWorker(worker, false);
            }
        }
        // Exercise the arena limit independently of global_control(1).
        tbb::global_control limit(tbb::global_control::max_allowed_parallelism, 2);
        require(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) == 2,
                "Arena test requires a process-wide limit larger than one");
        tbb::task_arena arena(1);
        arena.execute([&] {
            for (unsigned threads : {1U, 2U}) runArena(log, threads);
        });
        std::cout << "Production same-worker reuse and one-slot arena full/cancel/retry passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
