#include "harness.h"
#include "corpus.h"
#include "configuration.h"
#include "storagecontext.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <iostream>

namespace {
std::vector<std::string> names()
{
    std::vector<std::string> result;
    for (const auto* name : {"short1", "short16", "long16", "utf16", "mixed", "boolean",
                            "casefold", "regex", "empty", "prefilter", "oversized"})
        for (auto threads : {1, 2}) result.push_back(std::string(name) + "-t" + std::to_string(threads));
    return result;
}
Scenario makeScenario(const std::string& name)
{
    Scenario result;
    result.name = name;
    result.threads = name.back() == '2' ? 2U : 1U;
    const auto base = name.substr(0, name.size() - 3);
    const bool longLines = base == "long16" || base == "utf16";
    const size_t length = base == "oversized" ? experimental::MaxBatchBytes + 40 : longLines ? 4096 : 128;
    const size_t rows = base == "oversized" ? 32 : longLines ? 2048 : 8192;
    const int count = base == "short1" ? 1 : 16;
    QStringList needles;
    for (int i = 0; i < count; ++i) needles << "needle_" + QString::number(i) + "_end";
    QStringList quoted;
    for (const auto& n : needles) quoted << "\"" + n + "\"";
    result.query = RegularExpressionPattern{count == 1 ? needles.front() : quoted.join(" or "),
                                           true, false, count != 1, true};
    if (base == "casefold") result.query.isCaseSensitive = false;
    if (base == "regex") result.query = RegularExpressionPattern{"needle_[0-9]+_end", true, false, false, false};
    if (base == "empty") result.query = RegularExpressionPattern{"", true, false, false, true};
    if (base == "boolean") result.query = RegularExpressionPattern{
        "(\"needle_0_end\" or \"needle_1_end\") and not(\"blocked\")", true, false, true, true};
    if (base == "prefilter") result.prefilter = "\\[tag\\]";
    for (size_t row = 0; row < rows; ++row) {
        std::string line(length, 'x');
        const std::string prefix = base == "utf16" ? "中文😀\t[tag] " : "2026-11-18\tINFO [tag] ";
        line.replace(0, prefix.size(), prefix);
        if (row % 2 == 0) {
            const auto token = needles[static_cast<qsizetype>(row % static_cast<size_t>(count))].toStdString();
            line.replace(48, token.size(), token);
        }
        if (base == "mixed" && row % 10 == 0) line[31] = static_cast<char>(0xFF);
        if (base == "boolean" && row % 4 == 0) line.replace(80, 7, "blocked");
        result.bytes.append(line.data(), static_cast<qsizetype>(line.size()));
        result.bytes += '\n';
    }
    if (base == "utf16") {
        result.codec = "UTF-16LE";
        result.bytes = QTextCodec::codecForName(result.codec)->fromUnicode(QString::fromUtf8(result.bytes));
    }
    return result;
}
void output(const QJsonObject& row) { std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n'; }
void measure(const Scenario& s, bool candidate, bool reverse, bool profile)
{
    auto owned = s.path.isEmpty()
        ? std::make_unique<workerprobe::Fixture>(s.bytes, s.codec.constData())
        : std::make_unique<workerprobe::Fixture>(workerprobe::FileSource{s.path}, s.codec.constData(), s.timeoutMs);
    auto& fixture = *owned;
    const auto signature = inputHash(s, fixture.fileHash);
    const auto run = [&](bool backend, bool cancel = false) {
        return workerprobe::search(fixture, s.query, backend, s.threads, s.chunk, cancel,
                                   0, std::numeric_limits<uint64_t>::max(), s.timeoutMs);
    };
    fixture.log.setPrefilter(s.prefilter);
    experimental::resetDiagnostics(false);
    const auto qt = run(false);
    const auto digest = qt.digest();
    const auto check = [&](const auto& result) {
        workerprobe::require(result.digest() == digest, "Worker result differs from Qt oracle");
    };
    if (profile) {
        experimental::resetDiagnostics(true);
        const auto full = run(candidate);
        check(full);
        const auto stats = full.diagnostics;
        experimental::resetDiagnostics(false);
        const auto cancelled = run(candidate, true);
        output({{"scenario", QString::fromStdString(s.name)}, {"candidate", candidate ? "memchr" : "qt"},
                {"input_sha256", signature}, {"file_sha256", QString::fromLatin1(fixture.fileHash)},
                {"input_bytes", double(fixture.file.size())}, {"result_sha256", QString::fromStdString(digest)},
                {"matchers", double(stats.matchers)}, {"batches", double(stats.batches)},
                {"batched_rows", double(stats.rows)}, {"oversized_qt_lines", double(stats.oversized)},
                {"max_batch_rows", double(stats.maxRows)}, {"max_batch_bytes", double(stats.maxBytes)},
                {"cancel_requested", cancelled.cancelRequested}, {"cancel_ns", double(cancelled.cancelNs)},
                {"cancel_processed", double(cancelled.result.processedLines.get())},
                {"cancel_matched", double(cancelled.result.newMatches.cardinality())},
                {"complete_lines", double(full.result.processedLines.get())}, {"live_blocks", double(experimental::liveBlocks())}});
        return;
    }
    // Explicit warm search on both backends; each measured run still constructs a fresh worker/matchers.
    check(run(candidate));
    const std::array<bool, 4> order = reverse ? std::array<bool, 4>{true, false, false, true}
                                            : std::array<bool, 4>{false, true, true, false};
    for (const bool current : order) {
        const auto result = run(current && candidate);
        check(result);
        output({{"scenario", QString::fromStdString(s.name)}, {"candidate", candidate ? "memchr" : "qt"},
                {"role", current ? "candidate" : "reference"}, {"threads", int(s.threads)},
                {"input_sha256", signature}, {"file_sha256", QString::fromLatin1(fixture.fileHash)},
                {"input_bytes", double(fixture.file.size())}, {"result_sha256", QString::fromStdString(digest)},
                {"elapsed_ns", double(result.elapsedNs)}, {"rows", double(result.result.processedLines.get())},
                {"matched", double(result.result.newMatches.cardinality())}, {"chunk_rows", int(s.chunk)}});
    }
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"verify-integration", "Verify highlight positions and backend routing."},
        {"require-accelerated", "Fail integration verification unless an accelerated library executes."},
        {"verify", "Run worker integration and lifecycle contracts."},
        {"candidate", "memchr or identical Qt control", "backend", "qt"},
        {"corpus", "Version 1 external-file query manifest", "path"},
        {"case", "Exact generated scenario name", "name"}, {"reverse", "Reverse paired order."},
        {"profile", "Run untimed routing/cancellation diagnostics."}});
    parser.process(app);
    QTemporaryDir root;
    if (!root.isValid() || !StorageContext::install({StorageMode::CustomDirectory, root.path(),
        root.filePath("settings.ini"), true})) return 2;
    qRegisterMetaType<LoadingStatus>();
    qRegisterMetaType<LinesCount>();
    qRegisterMetaType<LineNumber>();
    Configuration::getSynced().setRegexpEnging(RegexpEngine::QRegularExpression);
    try {
        workerprobe::require(!parser.isSet("corpus")
            || (!parser.isSet("verify") && !parser.isSet("verify-integration") && !parser.isSet("case")),
            "Use --corpus separately from generated cases and verification modes");
        if (parser.isSet("verify-integration")) {
            workerprobe::verifyIntegration(parser.isSet("require-accelerated")); return 0;
        }
        workerprobe::require(!parser.isSet("require-accelerated"), "Use --require-accelerated with --verify-integration");
        if (parser.isSet("verify")) { workerprobe::verify(); return 0; }
        const auto candidate = parser.value("candidate");
        workerprobe::require(candidate == "qt" || candidate == "memchr", "Unknown backend");
        if (parser.isSet("corpus")) {
            workerprobe::require(!parser.isSet("case"), "Use either --corpus or --case");
            for (const auto& scenario : loadCorpus(parser.value("corpus")))
                measure(scenario, candidate == "memchr", parser.isSet("reverse"), parser.isSet("profile"));
            return 0;
        }
        bool measured = false;
        for (const auto& name : names()) {
            if (parser.isSet("case") && parser.value("case").toStdString() != name) continue;
            measure(makeScenario(name), candidate == "memchr", parser.isSet("reverse"), parser.isSet("profile"));
            measured = true;
        }
        workerprobe::require(measured, "Unknown scenario");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
