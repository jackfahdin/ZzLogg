#include "harness.h"
#include "configuration.h"
#include "regularexpression.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTest>
#include <iostream>
#include "wakeup.h"
#include <future>
#include <atomic>
#include <thread>
namespace workerprobe {
namespace {
RegularExpressionPattern literal(const QString& text, bool boolean = false, bool inverse = false)
{
    return {text, true, inverse, boolean, true};
}
void check(Fixture& fixture, const RegularExpressionPattern& pattern,
           const std::vector<uint64_t>& expected, bool fast, unsigned threads = 1,
           uint64_t first = 0, uint64_t last = std::numeric_limits<uint64_t>::max())
{
    experimental::resetDiagnostics(true);
    const auto qt = search(fixture, pattern, false, threads, 17, false, first, last);
    experimental::resetDiagnostics(true);
    const auto candidate = search(fixture, pattern, true, threads, 17, false, first, last);
    require(qt.digest() == candidate.digest(), "Full worker result/width/progress differs");
    SearchResultArray want;
    for (auto row : expected) want.add(row);
    if (!(candidate.result.newMatches == want)) {
        std::string actual;
        for (auto row : candidate.result.newMatches) actual += std::to_string(row) + ",";
        throw std::runtime_error("Fixed rows differ for " + pattern.pattern.toStdString() + ": " + actual);
    }
    for (const auto* result : {&qt, &candidate}) {
        require((result->cpuNs >= 0 && result->userCpuNs >= 0 && result->systemCpuNs >= 0
                    && result->cpuNs == result->userCpuNs + result->systemCpuNs
                    && result->voluntarySwitches >= 0 && result->involuntarySwitches >= 0)
                || (result->cpuNs == -1 && result->userCpuNs == -1 && result->systemCpuNs == -1
                    && result->voluntarySwitches == -1 && result->involuntarySwitches == -1),
                "Incomplete or inconsistent per-search process resource counters");
    }
    require(candidate.result.processedLines.get() == std::min(last, fixture.log.getNbLine().get()),
            "Complete search did not cover requested range");
    require(candidate.diagnostics.wakeSearches == (experimental::wakeupEnabled() ? 1U : 0U),
            "Worker ignored requested scheduling route");
    const bool assist = experimental::scheduling() == experimental::Scheduling::Assist;
    require(candidate.diagnostics.assistSearches == (assist ? 1U : 0U), "Worker ignored producer assistance");
    require(!assist || (candidate.diagnostics.pollSleeps == 0 && candidate.diagnostics.waits == 0),
            "Producer assistance fell back to a passive wait");
    require(!experimental::wakeupEnabled() || (candidate.diagnostics.pollSleeps == 0
                && candidate.diagnostics.releases == (std::min(last, fixture.log.getNbLine().get()) - first + 16) / 17),
            "Notification path polled or lost capacity release");
    require((candidate.firstHitNs >= 0) == !expected.empty(), "First-hit notification disagrees with fixed rows");
    require(candidate.firstProgressNs >= 0, "Missing intermediate progress notification");
    require((candidate.diagnostics.matchers != 0) == (fast || experimental::usesPcre2), "Incorrect query-level routing");
    require(candidate.diagnostics.maxBytes <= experimental::MaxBatchBytes
                && candidate.diagnostics.maxRows <= experimental::MaxBatchRows, "Batch exceeded limits");
}
}
void verifyWakeup()
{
    using namespace std::chrono_literals;
    // A release between failed check and actual sleep must not be lost.
    experimental::WakeupGate gate(5s);
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
    experimental::WakeupGate early;
    bool free = false;
    early.release([&] { free = true; });
    require(early.submit([&] { return free; }, [] { return false; }) && early.stats().waits == 0,
            "Release before submit was lost");
    experimental::WakeupGate cancelled;
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
    experimental::WakeupGate bounded;
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
void verify()
{
    verifyWakeup();
    if (experimental::usesPcre2) {
        // Actual worker bitmap order, boolean evaluation and inverse, including regex-only syntax.
        Fixture regexRows("ab\nac\nblocked ab\n中文😀\n\n");
        check(regexRows, {"\"a(?=b)\" and not(\"blocked\")", true, false, true, false}, {0}, true, 2);
        check(regexRows, {"\"a(?=b)\" and not(\"blocked\")", true, true, true, false}, {1, 2, 3, 4}, true, 2);
        require(experimental::metrics().programs == 4 && experimental::metrics().jitPrograms == 4
                    && experimental::metrics().direct == 10 && experimental::metrics().fallback == 0,
                "Regex boolean worker did not execute all PCRE programs");
        check(regexRows, {"^$", true, false, false, false}, {4}, true);
        check(regexRows, {"^\\C\\C$", true, false, false, false}, {0, 1}, true);
        require(experimental::metrics().fallbackPatterns == 1 && experimental::metrics().fallback == 5
                    && experimental::metrics().direct == 0, "Code-unit pattern did not fall back to Qt");
    }
    Fixture plain("a\nb\nab\nac\nnothing\n\na\"b\na.b\n");
    for (const unsigned threads : {1U, 2U}) {
        check(plain, literal("\"a\" and (not(\"b\") or \"c\")", true), {0, 3}, true, threads);
        check(plain, literal("\"a\" and (not(\"b\") or \"c\")", true, true), {1, 2, 4, 5, 6, 7}, true, threads);
        check(plain, literal("(\"a\" xor \"b\") and (\"a\" or \"c\")", true), {0, 3}, true, threads);
        check(plain, literal("\"a\" and \"a\" and \"ab\"", true), {2}, true, threads);
        check(plain, literal("\"a\\\"b\" or \"nothing\"", true), {4, 6}, true, threads);
        check(plain, literal("a"), {2, 3}, true, threads, 2, 5);
        check(plain, literal(""), {0, 1, 2, 3, 4, 5, 6, 7}, false, threads);
        check(plain, literal("\"a\" and \"\"", true), {0, 2, 3, 6, 7}, false, threads);
        check(plain, RegularExpressionPattern{"a.b", true, false, false, false}, {6, 7}, false, threads);
        check(plain, RegularExpressionPattern{"A", false, false, false, true}, {0, 2, 3, 6, 7}, false, threads);
    }
    Fixture unicode(QByteArray("Ä\nä\n中文😀\r\n"));
    // Use exact UTF-8 QByteArray construction instead of a user locale codec.
    check(unicode, RegularExpressionPattern{"ä", false, false, false, true}, {0, 1}, false);
    Fixture bad(QByteArray("ok\n\xFF\n\xEF\xBB\xBF" "needle\n") + QByteArray("needle\0tail", 11));
    check(bad, literal(QString::fromUtf8("�")), {1}, true);
    check(bad, literal("needle"), {2, 3}, true);
    if (experimental::usesPcre2) {
        require(experimental::metrics().direct == 2 && experimental::metrics().fallback == 2,
                "Worker did not preserve bad UTF-8 and BOM fallback");
    }
    check(bad, literal(QString::fromUtf8("needle\0tail", 11)), {3}, true);
    Fixture pref("[tag]ERROR\nok\nERROR\n");
    pref.log.setPrefilter("\\[tag\\]");
    check(pref, literal("ERROR"), {0, 2}, true);
    Fixture utf16(QTextCodec::codecForName("UTF-16LE")->fromUnicode(QString::fromUtf8("ok\n中文😀\n中文😀")), "UTF-16LE");
    check(utf16, literal("中文😀"), {1, 2}, true, 2);
    Fixture latin(QTextCodec::codecForName("ISO-8859-1")->fromUnicode(QString::fromUtf8("café\nok\n")), "ISO-8859-1");
    check(latin, literal("é"), {0}, true);

    std::string almost(experimental::MaxBatchBytes - 1, 'a'), oversized(experimental::MaxBatchBytes + 1, 'a');
    klogg::vector<std::string_view> boundaries{almost, "a", "", "a", oversized, "last"};
    require(experimental::batchEnd(boundaries, 0) == 3 && experimental::batchEnd(boundaries, 3) == 4
                && experimental::batchEnd(boundaries, 4) == 4 && experimental::batchEnd(boundaries, 5) == 6,
            "Byte boundary splitting differs");
    klogg::vector<std::string_view> empty(140, "");
    require(experimental::batchEnd(empty, 0) == 128, "Empty lines bypass row bound");
    Fixture longLine(QByteArray::fromStdString(oversized) + "\na\n");
    check(longLine, literal("a"), {0, 1}, true);
    require(experimental::metrics().oversized == 1, "Oversized line did not use Qt");

    QByteArray lots;
    for (int i = 0; i < 65536; ++i) lots += "ERROR some text\n";
    Fixture many(lots);
    for (bool memchr : {false, true}) {
        for (unsigned threads : {1U, 2U}) {
            experimental::setEnabled(memchr);
            auto& config = Configuration::get();
            config.setUseParallelSearch(threads > 1);
            config.setSearchThreadPoolSize(static_cast<int>(threads));
            config.setSearchReadBufferSizeLines(128);
            AtomicFlag cancelled;
            SearchData data;
            FullSearchOperation operation(many.log, cancelled, literal("ERROR"), 0_lnum, LineNumber{65536});
            QObject::connect(&operation, &SearchOperation::searchProgressed, QCoreApplication::instance(),
                [&](LinesCount, int percent, LineNumber) { if (percent < 100) cancelled.set(); }, Qt::DirectConnection);
            operation.run(data);
            require(static_cast<bool>(cancelled) && data.getNbMatches().get() > 0
                        && data.getNbMatches().get() < 65536, "Synchronous cancellation did not stop search");
            require(experimental::liveBlocks() == 0 && experimental::liveMatchers() == 0, "Cancellation leaked queued/unsubmitted blocks");
            AtomicFlag preCancelled(true);
            SearchData noResults;
            FullSearchOperation pre(many.log, preCancelled, literal("ERROR"), 0_lnum, LineNumber{65536});
            pre.run(noResults);
            require(noResults.getNbMatches().get() == 0 && experimental::liveBlocks() == 0,
                    "Pre-cancel processed data or leaked blocks");
            require(experimental::liveMatchers() == 0, "Pre-cancel leaked matchers");
            const auto stopped = search(many, literal("ERROR"), memchr, threads, 128, true);
            require(stopped.cancelRequested && stopped.result.newMatches.cardinality() < 65536,
                    "Async cancellation missed active search");
            const auto again = search(many, literal("ERROR"), memchr, threads, 128);
            require(again.result.newMatches.cardinality() == 65536 && again.result.processedLines.get() == 65536,
                    "Search after cancel retained incomplete state");
        }
    }
    for (bool memchr : {false, true}) {
        for (bool regex : {false, true}) {
            Fixture tail(regex ? QByteArray("prefix\nERROR") : QByteArray("prefix\nERR"));
            experimental::setEnabled(memchr);
            auto pattern = regex ? RegularExpressionPattern{"^ERROR$", true, false, false, false} : literal("ERROR");
            auto worker = std::make_unique<LogFilteredDataWorker>(tail.log);
            QSignalSpy finished(worker.get(), &LogFilteredDataWorker::searchFinished);
            const auto wait = [&] {
                QElapsedTimer deadline;
                deadline.start();
                while (finished.isEmpty() && deadline.elapsed() < 30000) QTest::qWait(1);
                require(!finished.isEmpty(), "Update search timeout");
                finished.clear();
            };
            worker->search(pattern, 0_lnum, LineNumber{2});
            wait();
            const auto initial = worker->getSearchResults();
            require(initial.newMatches.cardinality() == (regex ? 1U : 0U), "Initial tail mismatch");
            require(tail.file.seek(tail.file.size()), "Cannot seek append fixture");
            const QByteArray append = regex ? QByteArray("x\nERROR\n") : QByteArray("OR\nERROR\n");
            require(tail.file.write(append) == append.size() && tail.file.flush(), "Cannot append fixture");
            tail.reload();
            require(tail.log.getNbLine().get() == 3, "Updated file line count differs");
            worker->updateSearch(pattern, 0_lnum, LineNumber{3}, LineNumber{1});
            wait();
            const auto updated = worker->getSearchResults();
            require(updated.processedLines.get() == 3, "Update progress did not reach file end");
            require(updated.newMatches.contains(uint64_t{2})
                        && updated.newMatches.cardinality() == (regex ? 1U : 2U), "Tail update results differ");
            require(updated.removedMatches.contains(uint64_t{1}) == regex, "Old tail removal differs");
            worker.reset();
            require(experimental::liveBlocks() == 0 && experimental::liveMatchers() == 0, "Update leaked search blocks");
        }
    }
    Fixture emptyFile(QByteArray{});
    const auto emptyResult = search(emptyFile, literal("ERROR"), true);
    require(emptyResult.result.newMatches.isEmpty() && emptyResult.result.processedLines.get() == 0,
                "Empty file result differs");
    experimental::resetDiagnostics(false);
    std::cout << "Full worker contracts passed: real indexing, Qt routing, nested boolean/inverse, encodings, range, "
                 "byte bounds, long-line fallback, cancellation, repeat search, append/tail updates and empty files in 1/2 matching threads\n";
}
}
