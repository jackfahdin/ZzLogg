#include "configuration.h"
#include "logdata.h"
#include "logfiltereddataworker.h"
#include "storagecontext.h"
#include <QCoreApplication>
#include <QEventLoop>
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
void run(LogData& log, unsigned threads, bool cancel)
{
    Configuration::get().setUseParallelSearch(threads > 1);
    Configuration::get().setSearchThreadPoolSize(static_cast<int>(threads));
    Configuration::get().setSearchReadBufferSizeLines(128);
    LogFilteredDataWorker worker(log);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool expired = false, requested = false;
    unsigned finished = 0, terminal = 0;
    QObject::connect(&worker, &LogFilteredDataWorker::searchProgressed, &loop,
        [&](LinesCount, int percent, LineNumber) {
            if (percent == 100) ++terminal;
            if (cancel && percent < 100 && !requested) { requested = true; worker.interrupt(); }
        });
    QObject::connect(&worker, &LogFilteredDataWorker::searchFinished, &loop,
        [&] { ++finished; loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop,
        [&] { expired = true; worker.interrupt(); loop.quit(); });
    deadline.start(4000);
    worker.search({"ERROR", true, false, false, true}, 0_lnum, LineNumber{Rows});
    if (!finished) loop.exec();
    deadline.stop();
    require(!expired, "Single-slot search made no progress before deadline");
    require(finished == 1 && terminal == 1, "Search did not finish exactly once");
    const auto result = worker.getSearchResults();
    if (cancel) {
        require(requested && result.processedLines.get() < Rows, "Cancellation missed active search");
    } else {
        require(result.processedLines.get() == Rows && result.newMatches.cardinality() == Rows / 2,
                "Complete result lost rows");
        require(result.maxLength.get() == 11, "Tab-expanded width differs");
    }
    for (auto row : result.newMatches) require(row < Rows && row % 2 == 0, "Wrong matched row");
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
                run(log, threads, false);
                run(log, threads, true);
                run(log, threads, false);
            }
        }
        // Also cover a one-slot arena with a larger process-wide limit.
        tbb::task_arena arena(1);
        arena.execute([&] {
            AtomicFlag cancelled;
            SearchData data;
            FullSearchOperation operation(log, cancelled, {"ERROR", true, false, false, true}, 0_lnum, LineNumber{Rows});
            operation.run(data);
            require(data.getNbMatches().get() == Rows / 2, "One-slot arena lost matches");
        });
        std::cout << "Production worker single-slot full/cancel/research and one-slot arena passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
