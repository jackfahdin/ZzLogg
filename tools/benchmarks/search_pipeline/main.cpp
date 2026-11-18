#include "pipeline.h"
#include "configuration.h"
#include "storagecontext.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <iostream>
#ifdef __linux__
#include <sys/resource.h>
#endif

namespace {
struct Scenario {
    std::string name;
    pipeline::Query query;
    std::vector<std::string> lines;
    const char* encoding = "UTF-8";
    QString prefilter;
};
std::vector<std::string> scenarioNames()
{
    std::vector<std::string> names;
    for (const auto* encoding : {"ascii", "utf8", "utf16", "latin1"})
        for (const int length : {128, 4096})
            for (const int count : {1, 16, 64})
                names.push_back(std::string(encoding) + "-" + std::to_string(length) + "-p" + std::to_string(count));
    for (const auto* name : {"fallback-invalid", "fallback-case", "fallback-regex", "fallback-empty",
                            "prefilter", "near-miss", "overlap", "large-short"}) names.emplace_back(name);
    return names;
}
Scenario makeScenario(const std::string& name)
{
    Scenario s;
    s.name = name;
    const auto parts = QString::fromStdString(name).split('-');
    int length = 128, count = 16;
    size_t rows = 4096;
    QString prefix;
    if (parts.size() == 3) {
        length = parts[1].toInt();
        count = parts[2].mid(1).toInt();
        if (parts[0] == "utf8" || parts[0] == "utf16") prefix = "文😀";
        if (parts[0] == "latin1") { prefix = "é"; s.encoding = "ISO-8859-1"; }
        if (parts[0] == "utf16") s.encoding = "UTF-16LE";
        rows = length == 4096 ? 512 : 4096;
    }
    if (name == "near-miss" || name == "overlap") { length = 4096; count = 64; rows = 512; }
    if (name == "large-short") { count = 64; rows = 8193; }
    QStringList needles;
    for (int i = 0; i < count; ++i) {
        if (name == "near-miss") needles << QString(63, 'a') + 'b' + QString::number(i);
        else if (name == "overlap") needles << (i == count - 1 ? QString("missing") : QString(i + 1, 'a'));
        else needles << prefix + "needle_" + QString::number(i) + "_end";
    }
    if (name == "fallback-empty") needles[0] = "";
    if (name == "fallback-regex") needles[0] = "needle_[0-9]+_end";
    s.query = pipeline::Query::make(needles, count == 1 ? QString{} : QString("or"), false,
                                    name != "fallback-case", name != "fallback-regex");
    if (name == "prefilter") s.prefilter = "\\[tag\\]";
    for (size_t row = 0; row < rows; ++row) {
        std::string line(static_cast<size_t>(length), name == "near-miss" || name == "overlap" ? 'a' : 'x');
        if (name != "near-miss" && name != "overlap") {
            const auto text = needles[static_cast<qsizetype>(row % static_cast<size_t>(count))].toUtf8().toStdString();
            const auto header = prefix.toUtf8().toStdString() + "2026-11-18\tINFO [tag] ";
            line.replace(0, header.size(), header);
            if (row % 2 == 0) line.replace(48, text.size(), text);
            if (name == "fallback-invalid") line[32] = static_cast<char>(0xFF);
        }
        s.lines.push_back(std::move(line));
    }
    return s;
}
QString inputDigest(const Scenario& s, const LogData::RawLines& raw)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(raw.buffer.data(), static_cast<qsizetype>(raw.buffer.size())));
    hash.addData(s.query.publicPattern.pattern.toUtf8());
    hash.addData(s.encoding);
    hash.addData(s.prefilter.toUtf8());
    hash.addData(QByteArray::number(s.query.publicPattern.isCaseSensitive));
    hash.addData(QByteArray::number(s.query.publicPattern.isPlainText));
    return QString::fromLatin1(hash.result().toHex());
}
uint64_t scan(pipeline::Pipeline& p, LogData::RawLines& raw, int passes)
{
    uint64_t checksum = 0;
    for (int i = 0; i < passes; ++i) {
        const auto result = p.run(raw);
        checksum += result.matches.cardinality() + static_cast<uint64_t>(result.maxLength.get()) + result.processed;
    }
    return checksum;
}
void measure(const Scenario& s, const std::string& candidate, bool reverse)
{
    // Each side owns its decoder and transformed buffer; both are cold before first run.
    std::array<LogData::RawLines, 2> raw{
        pipeline::rawLines(s.lines, s.encoding, s.prefilter),
        pipeline::rawLines(s.lines, s.encoding, s.prefilter)};
    const auto hash = inputDigest(s, raw[0]);
    const std::array<std::string, 2> backends{"qt", candidate};
    std::array<std::unique_ptr<pipeline::Pipeline>, 2> matchers;
    std::array<qint64, 2> construct{}, first{};
    std::array<pipeline::Result, 2> results;
    std::array<int, 2> passes{};
    const std::array<size_t, 2> creation = reverse ? std::array<size_t, 2>{1, 0} : std::array<size_t, 2>{0, 1};
    for (const auto i : creation) {
        QElapsedTimer timer;
        timer.start();
        matchers[i] = std::make_unique<pipeline::Pipeline>(s.query, backends[i]);
        construct[i] = timer.nsecsElapsed();
        timer.restart();
        results[i] = matchers[i]->run(raw[i]);
        first[i] = timer.nsecsElapsed();
    }
    const auto digest = results[0].digest();
    if (results[1].digest() != digest) throw std::runtime_error("Pipeline mismatch: " + s.name);
    const auto expected = results[0].matches.cardinality() + static_cast<uint64_t>(results[0].maxLength.get())
        + results[0].processed;
    for (const auto i : creation) {
        QElapsedTimer timer;
        timer.start();
        if (scan(*matchers[i], raw[i], 1) != expected) throw std::runtime_error("Warmup mismatch");
        passes[i] = static_cast<int>(std::clamp<qint64>(10000000 / std::max<qint64>(1, timer.nsecsElapsed()), 1, 32));
    }
    const std::array<size_t, 4> order = reverse ? std::array<size_t, 4>{1, 0, 0, 1} : std::array<size_t, 4>{0, 1, 1, 0};
    for (const auto i : order) {
        QElapsedTimer timer;
        timer.start();
        const auto checksum = scan(*matchers[i], raw[i], passes[i]);
        const auto elapsed = timer.nsecsElapsed();
        if (checksum != expected * static_cast<uint64_t>(passes[i])) throw std::runtime_error("Timed mismatch");
        const QJsonObject row{{"scenario", QString::fromStdString(s.name)},
            {"candidate", QString::fromStdString(candidate)}, {"role", i == 0 ? "reference" : "candidate"},
            {"input_sha256", hash}, {"result_sha256", QString::fromStdString(digest)},
            {"lines", double(results[i].processed)}, {"patterns", double(s.query.patterns.size())},
            {"construct_ns", double(construct[i])}, {"first_ns", double(first[i])},
            {"ns_per_line", double(elapsed) / (passes[i] * double(results[i].processed))},
            {"passes", passes[i]}};
        std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n';
    }
}
void profile(const Scenario& s, const std::string& candidate)
{
    auto raw = pipeline::rawLines(s.lines, s.encoding, s.prefilter);
    pipeline::Pipeline p(s.query, candidate);
    // Warm once, then record diagnostics in a separate untimed process.
    const auto first = p.run(raw);
    const auto result = p.run(raw, {true, false, false});
    if (first.digest() != result.digest()) throw std::runtime_error("Profile changed results");
    const auto cancelled = p.run(raw, {true, false, true});
    qint64 peakRss = -1;
#ifdef __linux__
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) peakRss = usage.ru_maxrss;
#endif
    const QJsonObject row{{"scenario", QString::fromStdString(s.name)},
        {"candidate", QString::fromStdString(candidate)}, {"input_sha256", inputDigest(s, raw)},
        {"result_sha256", QString::fromStdString(result.digest())},
        {"conversion_ns", double(result.conversionNs)}, {"max_batch_ns", double(result.maxBatchNs)},
        {"max_bitmap_bytes", double(result.maxBitmapBytes)}, {"peak_process_rss_kib", double(peakRss)},
        {"cancel_processed_lines", double(cancelled.processed)},
        {"cancel_batch_ns", double(cancelled.maxBatchNs)}};
    std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n';
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"verify", "Run deterministic production-component contracts."},
                       {"candidate", "qt, memchr-line, memchr-128 or memchr-4096", "name", "memchr-128"},
                       {"reverse", "Reverse construction and ABBA order."},
                       {"case", "One exact generated scenario.", "name"},
                       {"profile", "One-case diagnostics in a fresh process; requires --case."}});
    parser.process(app);
    QTemporaryDir settings;
    if (!settings.isValid() || !StorageContext::install({StorageMode::CustomDirectory,
        settings.path(), settings.filePath("storage.ini"), true})) return 2;
    app.setOrganizationName("ZzLoggProbe");
    app.setApplicationName("SearchPipeline");
    Configuration::getSynced().setRegexpEnging(RegexpEngine::QRegularExpression);
    try {
        if (parser.isSet("verify")) { pipeline::verify(); return 0; }
        if (parser.isSet("profile") && !parser.isSet("case")) throw std::runtime_error("Profile needs --case");
        bool measured = false;
        for (const auto& name : scenarioNames()) {
            if (parser.isSet("case") && parser.value("case").toStdString() != name) continue;
            const auto s = makeScenario(name);
            const auto candidate = parser.value("candidate").toStdString();
            if (parser.isSet("profile")) profile(s, candidate);
            else measure(s, candidate, parser.isSet("reverse"));
            measured = true;
        }
        if (!measured) throw std::runtime_error("Unknown scenario");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
