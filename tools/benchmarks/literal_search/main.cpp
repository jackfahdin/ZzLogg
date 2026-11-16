#include "matcher.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
struct Scenario {
    std::string encoding;
    int lineBytes;
    int count;
    int needleLength;
    int hitPercent;
    bool dense = false;
    std::string name() const {
        return encoding + "-" + std::to_string(lineBytes) + "-p" + std::to_string(count)
            + "-n" + std::to_string(needleLength) + "-h" + std::to_string(hitPercent);
    }
};
struct Corpus {
    Patterns patterns;
    std::vector<std::string> storage;
    Lines lines() const {
        Lines views;
        for (const auto& line : storage) views.push_back(line);
        return views;
    }
};
Corpus makeCorpus(const Scenario& s)
{
    Corpus corpus;
    for (int i = 0; i < s.count; ++i) {
        QString needle;
        if (s.dense)
            needle = i + 1 == s.count ? "missing" : QString(i + 1, QChar('a'));
        else if (s.encoding == "near-miss")
            needle = QString(s.needleLength - 4, QChar('a')) + QString("%1z").arg(i, 3, 10, QChar('0'));
        else {
            needle = QString("q%1z").arg(i, 2, 10, QChar('0'));
            if (s.encoding == "utf8") needle = QStringLiteral("文") + needle;
            needle += QString(s.needleLength - 4, QChar('v'));
        }
        corpus.patterns.emplace_back(needle, true, false, false, true);
    }
    std::mt19937 random{0x1111U};
    constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz 0123456789=:";
    for (int row = 0; row < 128; ++row) {
        std::string line;
        const bool hit = (row % 8) * 100 < s.hitPercent * 8;
        const auto needle = hit ? corpus.patterns[row % s.count].pattern.toUtf8().toStdString() : "";
        const size_t target = static_cast<size_t>(s.lineBytes) - needle.size();
        if (s.dense || s.encoding == "near-miss") line.assign(s.lineBytes, 'a');
        else {
            const std::string unit = s.encoding == "utf8" ? "INFO 日志😀 " : "";
            while (!unit.empty() && line.size() + unit.size() <= target) line += unit;
            while (line.size() < target) line += alphabet[random() % (sizeof(alphabet) - 1)];
            if (s.encoding == "invalid")
                for (size_t pos = 3; pos < line.size(); pos += 31) line[pos] = '\xFF';
            if (row % 2) line += needle;
            else line.insert(0, needle);
        }
        corpus.storage.push_back(std::move(line));
    }
    return corpus;
}
uint64_t checksum(std::string_view result)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < result.size(); ++i)
        sum += static_cast<unsigned char>(result[i]) * (i + 1);
    return sum;
}
uint64_t scan(const LiteralMatcher& matcher, const Lines& lines, int passes)
{
    uint64_t sum = 0;
    for (int i = 0; i < passes; ++i) sum += checksum(matcher.scan(lines));
    return sum;
}
QString digest(std::string_view data)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(data.data(), static_cast<qsizetype>(data.size())),
        QCryptographicHash::Sha256).toHex());
}
void measure(const Scenario& scenario, const std::string& candidate, bool reverse)
{
    const auto corpus = makeCorpus(scenario);
    const auto lines = corpus.lines();
    QCryptographicHash inputHash(QCryptographicHash::Sha256);
    auto hashPart = [&](std::string_view data) {
        inputHash.addData(QByteArray::number(data.size()));
        inputHash.addData(QByteArrayView(":"));
        inputHash.addData(QByteArrayView(data.data(), static_cast<qsizetype>(data.size())));
    };
    for (const auto& p : corpus.patterns) hashPart(p.pattern.toUtf8().toStdString());
    for (const auto& line : lines) hashPart(line);
    qsizetype minNeedle = corpus.patterns.front().pattern.toUtf8().size();
    qsizetype maxNeedle = minNeedle;
    for (const auto& p : corpus.patterns) {
        minNeedle = std::min(minNeedle, p.pattern.toUtf8().size());
        maxNeedle = std::max(maxNeedle, p.pattern.toUtf8().size());
    }
    const std::array<std::string, 2> names{"qt", candidate};
    std::array<std::unique_ptr<LiteralMatcher>, 2> matchers;
    std::array<qint64, 2> constructNs{}, firstNs{};
    std::array<std::string, 2> results;
    std::array<int, 2> passes{};
    const std::array<int, 2> creationOrder = reverse ? std::array<int, 2>{1, 0}
                                                  : std::array<int, 2>{0, 1};
    for (const int i : creationOrder) {
        QElapsedTimer timer;
        timer.start();
        matchers[i] = makeMatcher(names[i], corpus.patterns);
        constructNs[i] = timer.nsecsElapsed();
        timer.restart();
        results[i] = matchers[i]->scan(lines);
        firstNs[i] = timer.nsecsElapsed();
    }
    if (results[0] != results[1]) throw std::runtime_error("Bitmap mismatch: " + scenario.name());
    int matchedLines = 0;
    for (size_t row = 0; row < lines.size(); ++row) {
        const auto offset = row * corpus.patterns.size();
        const auto cells = std::string_view(results[0]).substr(offset, corpus.patterns.size());
        matchedLines += cells.find(char(1)) != cells.npos;
    }
    const auto expected = checksum(results[0]);
    for (const int i : creationOrder) {
        QElapsedTimer timer;
        timer.start();
        const auto warm = scan(*matchers[i], lines, 1);
        const auto elapsed = std::max<qint64>(timer.nsecsElapsed(), 1);
        if (warm != expected) throw std::runtime_error("Warmup mismatch");
        passes[i] = static_cast<int>(std::clamp<qint64>(5000000 / elapsed, 1, 4096));
    }
    const std::array<int, 4> order = reverse ? std::array<int, 4>{1, 0, 0, 1}
                                           : std::array<int, 4>{0, 1, 1, 0};
    int sample = 0;
    for (const int i : order) {
        QElapsedTimer timer;
        timer.start();
        const auto sum = scan(*matchers[i], lines, passes[i]);
        const auto elapsed = timer.nsecsElapsed();
        if (sum != expected * passes[i]) throw std::runtime_error("Timed checksum mismatch");
        const QJsonObject row{
            {"scenario", QString::fromStdString(scenario.name())},
            {"encoding", QString::fromStdString(scenario.encoding)},
            {"line_bytes", scenario.lineBytes}, {"patterns", scenario.count},
            {"needle_base_length", scenario.needleLength}, {"target_line_hit_percent", scenario.hitPercent},
            {"actual_matched_lines", matchedLines},
            {"actual_matched_cells", double(std::count(results[0].begin(), results[0].end(), char(1)))},
            {"needle_utf8_min_bytes", double(minNeedle)}, {"needle_utf8_max_bytes", double(maxNeedle)},
            {"candidate", QString::fromStdString(candidate)}, {"role", i == 0 ? "reference" : "candidate"},
            {"backend", QString::fromStdString(names[i])}, {"sample", sample++},
            {"input_sha256", QString::fromLatin1(inputHash.result().toHex())},
            {"result_sha256", digest(results[i])}, {"qt", qVersion()},
            {"construct_ns", double(constructNs[i])},
            {"first_batch_ns", double(firstNs[i])},
            {"automaton_bytes", double(matchers[i]->automatonBytes())},
            {"batch_lines", double(lines.size())}, {"passes", passes[i]},
            {"ns_per_line", double(elapsed) / (passes[i] * lines.size())}
        };
        std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n';
    }
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"verify", "Only run deterministic compatibility checks."},
                       {"candidate", "Candidate backend; qt runs an identical-code control.", "name", "std"},
                       {"reverse", "Reverse construction and ABBA order."},
                       {"case", "Only this exact scenario name.", "name"}});
    parser.process(app);
    try {
        if (parser.isSet("verify")) { verifyContracts(); return 0; }
        const auto candidate = parser.value("candidate").toStdString();
        makeMatcher(candidate, {}); // Reject unavailable candidates even for an empty selection.
        std::vector<Scenario> scenarios;
        for (const auto* encoding : {"ascii", "utf8", "invalid"})
            for (const int length : {128, 4096})
                for (const int count : {1, 4, 16, 64})
                    for (const int needle : {4, 24})
                        for (const int hit : {0, 50, 100})
                            scenarios.push_back({encoding, length, count, needle, hit});
        scenarios.push_back({"dense-overlap", 4096, 64, 0, 100, true});
        scenarios.push_back({"near-miss", 4096, 1, 24, 0});
        scenarios.push_back({"near-miss", 4096, 64, 24, 0});
        bool measured = false;
        for (const auto& scenario : scenarios) {
            if (parser.isSet("case") && parser.value("case").toStdString() != scenario.name()) continue;
            measure(scenario, candidate, parser.isSet("reverse"));
            measured = true;
        }
        if (!measured) throw std::runtime_error("No scenario selected");
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
