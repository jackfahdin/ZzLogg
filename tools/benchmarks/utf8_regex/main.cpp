#include "matcher.h"
#include <pcre2.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <variant>

namespace {
using Clock = std::chrono::steady_clock;
using Any = std::variant<DefaultRegularExpressionMatcher, utf8regex::Matcher>;
struct Case { QString id, file; QStringList patterns; bool sensitive = true; };
std::vector<Case> cases()
{
    return {{"dmesg-errno", "dmesg", {"\\berror -(?:5|19|110)\\b"}},
        {"dmesg-lookahead", "dmesg", {"i2c(?=.*timeout)"}},
        {"dmesg-casefold", "dmesg", {"TIMEOUT"}, false},
        {"logcat-levels", "logcat", {"\\s[EF]\\s+(?:AndroidRuntime|CameraService)\\s*:"}},
        {"logcat-negative", "logcat", {"binder(?! debug)"}},
        {"app-unicode", "app", {"温度=[0-9]+℃"}},
        {"app-multi16", "app", {"ioctl.*(?:completed|failed)", "heartbeat", "温度=[0-9]+", "SIGSEGV", "ANR",
            "Exception", "retry=[1-3]", "payload_end", "onResume", "checksum=[0-9a-f]+", "(?:DriverClient|NativeBridge)",
            "(?<=message\":\")java\\.io\\.IOException", "/dev/my_sensor", "采集正常😀",
            "(?<x>com)\\.example\\.driverlab.*\\k<x>", "zzlogg-never"}},
        {"boundaries", "boundaries", {"^EDGE", "\\x{FFFD}", "\\x{0}", "^$", "\\C\\C", "payload_end", "😀", "$"}}};
}
qint64 elapsed(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QString hash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString configuration(int what)
{
    std::array<char, 128> text{};
    require(pcre2_config(what, text.data()) >= 0, "Cannot read PCRE version");
    return QString::fromLatin1(text.data());
}
QByteArray scan(const Any& matcher, const std::vector<std::string_view>& lines, size_t count,
                utf8regex::Routing* route = nullptr)
{
    require(count == 0 || lines.size() <= size_t(std::numeric_limits<qsizetype>::max()) / count,
            "Bitmap exceeds Qt length range");
    QByteArray result;
    result.reserve(static_cast<qsizetype>(lines.size() * count));
    for (const auto line : lines) {
        MatchedPatterns row;
        if (std::holds_alternative<utf8regex::Matcher>(matcher))
            row = std::get<utf8regex::Matcher>(matcher).match(line, route);
        else {
            row = std::get<DefaultRegularExpressionMatcher>(matcher).match(line);
            if (route) route->fallback += count;
        }
        require(row.size() == count, "Result pattern count differs");
        result.append(row.data(), static_cast<qsizetype>(row.size()));
    }
    return result;
}
void output(QJsonObject record) { std::cout << QJsonDocument(record).toJson(QJsonDocument::Compact).constData() << '\n'; }
void run(const Case& c, const QString& directory, bool candidate, bool reverse)
{
    QFile file(QDir(directory).filePath(c.file + ".log"));
    require(file.open(QIODevice::ReadOnly), "Cannot open corpus file");
    const auto bytes = file.readAll();
    require(file.error() == QFile::NoError && !bytes.isEmpty(), "Cannot read corpus or empty file");
    std::vector<std::string_view> lines;
    qsizetype start = 0;
    while (start < bytes.size()) {
        const auto newline = bytes.indexOf('\n', start);
        const auto end = newline < 0 ? bytes.size() : newline;
        lines.emplace_back(bytes.constData() + start, size_t(end - start));
        start = end + 1;
    }
    utf8regex::Patterns patterns;
    for (const auto& p : c.patterns) patterns.emplace_back(p, c.sensitive, false, false, false);
    const Any oracle{std::in_place_type<DefaultRegularExpressionMatcher>, patterns};
    const auto expected = scan(oracle, lines, patterns.size());
    QJsonArray queries, hits;
    for (size_t i = 0; i < patterns.size(); ++i) {
        queries.append(patterns[i].pattern);
        qint64 matches = 0;
        for (size_t line = 0; line < lines.size(); ++line)
            matches += expected[static_cast<qsizetype>(line * patterns.size() + i)] != 0;
        hits.append(matches);
    }
    QJsonObject common{{"case", c.id}, {"candidate", candidate ? "pcre2" : "qt"}, {"patterns", queries},
        {"case_sensitive", c.sensitive}, {"hits", hits}, {"rows", qint64(lines.size())},
        {"input_bytes", bytes.size()}, {"file_sha256", hash(bytes)}, {"result_sha256", hash(expected)},
        {"qt_version", qVersion()}, {"pcre2_version", configuration(PCRE2_CONFIG_VERSION)},
        {"unicode_version", configuration(PCRE2_CONFIG_UNICODE_VERSION)},
        {"qt_jit_environment", QString::fromLatin1(qgetenv("QT_ENABLE_REGEXP_JIT"))}};
    std::unique_ptr<Any> left, right;
    for (bool current : {reverse, !reverse}) {
        const auto buildStart = Clock::now();
        auto matcher = current && candidate
            ? std::make_unique<Any>(std::in_place_type<utf8regex::Matcher>, patterns)
            : std::make_unique<Any>(std::in_place_type<DefaultRegularExpressionMatcher>, patterns);
        const auto buildNs = elapsed(buildStart);
        const auto firstStart = Clock::now();
        const auto actual = scan(*matcher, lines, patterns.size());
        const auto firstNs = elapsed(firstStart);
        require(actual == expected, "First full result differs from Qt");
        auto record = common;
        record.insert("phase", "cold"); record.insert("role", current ? "candidate" : "reference");
        record.insert("build_ns", buildNs); record.insert("elapsed_ns", firstNs);
        record.insert("total_ns", buildNs + firstNs); output(record);
        (current ? right : left) = std::move(matcher);
    }
    for (bool current : (reverse ? std::array<bool,4>{true,false,false,true} : std::array<bool,4>{false,true,true,false})) {
        const auto begin = Clock::now();
        const auto actual = scan(current ? *right : *left, lines, patterns.size());
        const auto ns = elapsed(begin);
        require(actual == expected, "Hot full result differs from Qt");
        auto record = common;
        record.insert("phase", "hot"); record.insert("role", current ? "candidate" : "reference");
        record.insert("elapsed_ns", ns); output(record);
    }
    utf8regex::Routing route;
    require(scan(*right, lines, patterns.size(), &route) == expected, "Profile full result differs");
    utf8regex::Info info;
    if (candidate) info = std::get<utf8regex::Matcher>(*right).info();
    auto record = common;
    record.insert("phase", "profile"); record.insert("direct", qint64(route.direct));
    record.insert("fallback", qint64(route.fallback)); record.insert("runtime_errors", qint64(route.runtimeErrors));
    record.insert("programs", qint64(info.programs)); record.insert("fallback_patterns", qint64(info.fallbackPatterns));
    record.insert("jit_programs", qint64(info.jitPrograms)); record.insert("code_bytes", qint64(info.codeBytes));
    record.insert("jit_bytes", qint64(info.jitBytes)); output(record);
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        auto args = app.arguments();
        if (args.size() == 2 && args[1] == "--verify") { utf8regex::verify(); return 0; }
        bool reverse = args.size() == 6 && args.back() == "--reverse";
        if (reverse) args.removeLast();
        require(args.size() == 5 && args[1] == "--corpus" && args[3] == "--candidate"
            && (args[4] == "qt" || args[4] == "pcre2"), "Use --verify or --corpus DIR --candidate qt|pcre2 [--reverse]");
        for (const auto& c : cases()) run(c, args[2], args[4] == "pcre2", reverse);
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
