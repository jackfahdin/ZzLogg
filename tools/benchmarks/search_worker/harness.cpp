#include "harness.h"
#include "configuration.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <stdexcept>
namespace workerprobe {
void require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
namespace {
void loaded(QSignalSpy& spy, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (spy.isEmpty() && timer.elapsed() < timeoutMs) QTest::qWait(1);
    require(!spy.isEmpty(), "Indexing timeout");
    require(qvariant_cast<LoadingStatus>(spy.last().at(0)) == LoadingStatus::Successful, "Indexing failed");
}
}
Fixture::Fixture(const QByteArray& bytes, const char* codec)
{
    require(file.open(), "Cannot create file");
    require(file.write(bytes) == bytes.size() && file.flush(), "Cannot write fixture");
    fileHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    initialize(codec);
}
Fixture::Fixture(FileSource source, const char* codec, int timeoutMs) : timeoutMs_(timeoutMs)
{
    require(QTextCodec::codecForName(codec) != nullptr && timeoutMs > 0, "Invalid input codec or timeout");
    const QFileInfo before(source.path);
    require(before.isFile(), "Input must be a regular file");
    const auto sourceSize = before.size();
    const auto sourceModified = before.lastModified();
    QFile input(source.path);
    require(input.open(QIODevice::ReadOnly) && file.open(), "Cannot open input or temporary snapshot");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    qint64 total = 0;
    for (;;) {
        const auto size = input.read(block.data(), block.size());
        require(size >= 0, "Input read failed");
        if (size == 0) break;
        require(file.write(block.constData(), size) == size, "Snapshot write failed");
        hash.addData(QByteArrayView(block.constData(), size));
        total += size;
    }
    const QFileInfo after(source.path);
    require(total == sourceSize && sourceSize == after.size()
                && sourceModified == after.lastModified(), "Input changed while copying");
    require(file.flush(), "Snapshot flush failed");
    fileHash = hash.result().toHex();
    initialize(codec);
}
void Fixture::initialize(const char* codec)
{
    QSignalSpy spy(&log, &LogData::loadingFinished);
    log.attachFile(file.fileName());
    loaded(spy, timeoutMs_);
    reload(codec);
}
void Fixture::reload(const char* codec)
{
    QSignalSpy spy(&log, &LogData::loadingFinished);
    log.reload(QTextCodec::codecForName(codec));
    loaded(spy, timeoutMs_);
    log.setDisplayEncoding(codec);
}
std::string Outcome::digest() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto row : result.newMatches) hash.addData(QByteArray::number(row) + ',');
    hash.addData(QByteArray::number(result.maxLength.get()) + ':'
                 + QByteArray::number(result.processedLines.get()));
    for (auto row : result.removedMatches) hash.addData('r' + QByteArray::number(row) + ',');
    return hash.result().toHex().toStdString();
}
Outcome search(Fixture& fixture, const RegularExpressionPattern& pattern, bool memchr,
               unsigned threads, unsigned chunk, bool cancel, uint64_t start, uint64_t end, int timeoutMs)
{
    RegularExpression validation(pattern);
    require(validation.isValid(), "Probe query is not valid production syntax");
    auto& config = Configuration::get();
    config.setUseParallelSearch(threads > 1);
    config.setSearchThreadPoolSize(static_cast<int>(threads));
    config.setSearchReadBufferSizeLines(static_cast<int>(chunk));
    experimental::setEnabled(memchr);
    Outcome out;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool finished = false, expired = false;
    auto worker = std::make_unique<LogFilteredDataWorker>(fixture.log);
    QElapsedTimer timer;
    qint64 cancelAt = 0;
    QObject::connect(worker.get(), &LogFilteredDataWorker::searchProgressed, &loop,
        [&](LinesCount, int percent, LineNumber) {
            out.progress.push_back(percent);
            if (cancel && percent < 100 && !out.cancelRequested) {
                out.cancelRequested = true;
                cancelAt = timer.nsecsElapsed();
                worker->interrupt();
            }
        });
    QObject::connect(worker.get(), &LogFilteredDataWorker::searchFinished, &loop,
        [&] { finished = true; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        expired = true; worker->interrupt(); loop.quit();
    });
    timer.start();
    timeout.start(timeoutMs);
    const auto last = std::min(end, fixture.log.getNbLine().get());
    worker->search(pattern, LineNumber{start}, LineNumber{last});
    if (!finished) loop.exec();
    timeout.stop();
    out.result = worker->getSearchResults();
    worker.reset(); // Also joins the operation and destroys its TBB graph.
    out.elapsedNs = timer.nsecsElapsed();
    if (out.cancelRequested) out.cancelNs = out.elapsedNs - cancelAt;
    require(!expired && finished, "Worker search timeout");
    require(experimental::liveBlocks() == 0, "Search leaked live blocks");
    require(!out.progress.empty() && out.progress.back() == 100, "Missing terminal progress");
    require(std::is_sorted(out.progress.begin(), out.progress.end()), "Progress regressed");
    require(std::count(out.progress.begin(), out.progress.end(), 100) == 1, "Duplicate terminal progress");
    out.diagnostics = experimental::metrics();
    return out;
}
}
