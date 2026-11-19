// Explicit P12 component probe. Never linked into the application.
#include "hsregularexpression.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>

// Debugger measurement boundaries; no counters or instrumentation in production.
extern "C" Q_NEVER_INLINE void qt_regex_probe_begin() { }
extern "C" Q_NEVER_INLINE void qt_regex_probe_end() { }

namespace {
using Clock = std::chrono::steady_clock;
using Lines = std::vector<std::string>;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
qint64 elapsed(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
MatchedPatterns match(const MatcherVariant& matcher, std::string_view line)
{
    return std::visit([&](const auto& m) { return m.match(line); }, matcher);
}
MatcherVariant prefilter(const klogg::vector<RegularExpressionPattern>& patterns)
{
#ifdef KLOGG_HAS_HS
    HsRegularExpression expression(patterns);
    require(expression.isValid(), "Invalid expression");
    auto matcher = expression.createMatcher();
    require(std::holds_alternative<HsPrefilterMatcher>(matcher), "Actual accelerated prefilter required");
    return matcher; // Verify ownership after the source expression is destroyed.
#else
    (void)patterns;
    throw std::runtime_error("Build with Vectorscan/Hyperscan for prefilter measurement");
#endif
}
QByteArray scan(const MatcherVariant& matcher, const Lines& lines, size_t patterns)
{
    QByteArray result;
    result.reserve(static_cast<qsizetype>(lines.size() * patterns));
    for (const auto& line : lines) {
        const auto row = match(matcher, line);
        require(row.size() == patterns, "Lost pattern position");
        result.append(row.data(), static_cast<qsizetype>(row.size()));
    }
    return result;
}
QString hash(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
klogg::vector<RegularExpressionPattern> patternsFor(const QString& name)
{
    QStringList expressions;
    if (name == "dmesg") expressions = {"i2c(?=.*timeout)"};
    else if (name == "logcat") expressions = {"binder(?! debug)"};
    else if (name == "app") expressions = {
        "(?<=message\":\")java\\.io\\.IOException", "(?<=message\":\")SIGSEGV", "(?<=message\":\")ANR"};
    else if (name == "reuse") expressions = {"i2c(?=.*timeout)", "binder(?! debug)"};
    else throw std::runtime_error("Unknown case");
    klogg::vector<RegularExpressionPattern> result;
    for (const auto& expression : expressions) result.emplace_back(expression);
    return result;
}
void verifyReuse()
{
    const auto patterns = patternsFor("reuse");
    const Lines lines{"i2c transfer timeout binder transaction", "i2c completed binder debug txn", "absent", ""};
    const QByteArray expected("\1\1\0\0\0\0\0\0", 8);
    const auto matcher = prefilter(patterns);
    require(scan(matcher, lines, patterns.size()) == expected, "Warmup result differs");
    qt_regex_probe_begin();
    for (int i = 0; i < 64; ++i)
        require(scan(matcher, lines, patterns.size()) == expected, "Repeated result differs");
    qt_regex_probe_end();
    std::cout << "Reuse workload results passed\n";
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args == QStringList{args.front(), "--reuse"}) { verifyReuse(); return 0; }
        require(args.size() == 7 && args[1] == "--case" && args[3] == "--input"
                    && args[5] == "--engine", "Use --reuse or --case dmesg|logcat|app --input FILE --engine qt|prefilter");
        const auto patterns = patternsFor(args[2]);
        require(args[6] == "qt" || args[6] == "prefilter", "Unknown engine");
        QFile file(args[4]);
        require(file.open(QIODevice::ReadOnly), "Cannot open input");
        const auto bytes = file.readAll();
        require(file.error() == QFile::NoError && !bytes.isEmpty(), "Cannot read input or empty corpus");
        Lines lines;
        qsizetype start = 0;
        while (start < bytes.size()) {
            const auto newline = bytes.indexOf('\n', start);
            const auto end = newline < 0 ? bytes.size() : newline;
            lines.emplace_back(bytes.constData() + start, static_cast<size_t>(end - start));
            start = end + 1;
        }
        const MatcherVariant reference{DefaultRegularExpressionMatcher(patterns)};
        const auto expected = scan(reference, lines, patterns.size());
        const auto setupStart = Clock::now();
        const auto matcher = args[6] == "prefilter" ? prefilter(patterns)
            : MatcherVariant{DefaultRegularExpressionMatcher(patterns)};
        const auto setupNs = elapsed(setupStart);
        const auto firstStart = Clock::now();
        const auto first = match(matcher, lines.front());
        const auto firstNs = elapsed(firstStart);
        require(QByteArray(first.data(), static_cast<qsizetype>(first.size()))
                    == expected.first(static_cast<qsizetype>(patterns.size())), "First row differs");
        const auto scanStart = Clock::now();
        const auto initial = scan(matcher, lines, patterns.size());
        const auto scanNs = elapsed(scanStart);
        require(initial == expected, "First full result differs");
        const auto hotStart = Clock::now();
        const auto hot = scan(matcher, lines, patterns.size());
        const auto hotNs = elapsed(hotStart);
        require(hot == expected, "Hot full result differs");
        QJsonArray queries, hits;
        for (size_t p = 0; p < patterns.size(); ++p) {
            queries.append(patterns[p].pattern);
            qint64 count = 0;
            for (size_t row = 0; row < lines.size(); ++row)
                count += expected[static_cast<qsizetype>(row * patterns.size() + p)] != 0;
            hits.append(count);
        }
        const QJsonObject output{{"case", args[2]}, {"engine", args[6]}, {"patterns", queries},
            {"hits", hits}, {"rows", static_cast<qint64>(lines.size())}, {"bytes", bytes.size()},
            {"input_sha256", hash(bytes)}, {"result_sha256", hash(expected)},
            {"setup_ns", setupNs}, {"first_line_ns", firstNs}, {"first_scan_ns", scanNs},
            {"hot_scan_ns", hotNs}, {"qt_version", qVersion()},
            {"jit_environment", QString::fromLatin1(qgetenv("QT_ENABLE_REGEXP_JIT"))}};
        std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
