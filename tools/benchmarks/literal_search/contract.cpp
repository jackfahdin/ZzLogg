#include "matcher.h"
#include <QByteArray>
#include <QStringDecoder>
#include <array>
#include <random>
#include <stdexcept>
#include <iostream>

namespace {
class ExpectedInvalidRegexMessages {
public:
    ExpectedInvalidRegexMessages() : previous_(qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext&, const QString& message) {
            if (type != QtWarningMsg || message != QStringLiteral(
                "QRegularExpressionPrivate::doMatch(): called on an invalid QRegularExpression object"))
                std::cerr << message.toStdString() << '\n';
        })) {}
    ~ExpectedInvalidRegexMessages() { qInstallMessageHandler(previous_); }
private:
    QtMessageHandler previous_;
};
RegularExpressionPattern literal(const QString& text, bool sensitive = true)
{
    return {text, sensitive, false, false, true};
}
void compare(const Patterns& patterns, const Lines& lines)
{
    const auto expected = makeMatcher("qt", patterns)->scan(lines);
    if (expected.size() != patterns.size() * lines.size())
        throw std::runtime_error("Invalid reference shape");
    for (const auto& name : backends()) {
        const auto matcher = makeMatcher(name, patterns);
        if (matcher->scan(lines) != expected || matcher->scan(lines) != expected)
            throw std::runtime_error("Literal semantics mismatch: " + name);
    }
}
}

void verifyContracts()
{
    const std::vector<std::string> storage{
        "", "a", "aaa ababa banana", "a.b axb", "中文😀", "ÉéΣσςKkſs",
        std::string("a\0b\0", 4), "\xFF", "\xC0\xAF", "\xED\xA0\x80",
        "\xF4\x90\x80\x80", "\xE4\xB8", "\x80\xBF", "\xEF\xBB\xBF" "abc",
        "x\xEF\xBB\xBF" "abc", "\xEF\xBF\xBD", "\r", "a\r\nb"
    };
    Lines lines;
    for (const auto& input : storage) lines.push_back(input);
    const std::string bounded = "outside中文suffix";
    lines.push_back(std::string_view(bounded).substr(7, 6));
    lines.push_back(std::string_view(bounded).substr(7, 5));
    lines.push_back(std::string_view(bounded).substr(8, 5));
    const Patterns basic{literal("a"), literal("aa"), literal("a"), literal("aba"),
                         literal("banana"), literal("not present"), literal("a.b"),
                         literal("中文"), literal("😀"), literal(QString(QChar(0))),
                         literal(QString(QChar(0xfffd))), literal(QString(QChar(0xfeff)))};
    compare(basic, lines);
    for (const auto& pattern : basic) compare({pattern}, lines);
    compare({}, lines);
    compare(basic, {});
    compare(basic, {std::string_view{}});
    compare({literal(""), literal("a")}, lines);
    // Rejecting invalid queries is authoritative; also prove the candidate's whole-query fallback.
    if (static_cast<QRegularExpression>(literal(QString(QChar(0xd800)))).isValid())
        throw std::runtime_error("Qt unexpectedly accepted a lone surrogate query");
    {
        ExpectedInvalidRegexMessages expectedWarnings;
        compare({literal(QString(QChar(0xd800))), literal("a")}, {"a", "", "\xFF"});
    }
    compare({literal("é", false), literal("σ", false), literal("k", false), literal("s", false)}, lines);
    compare({literal("a"), RegularExpressionPattern("a.b")}, lines);
    const QByteArray utf16le = QByteArray::fromHex("2d4e87653dd800de");
    QStringDecoder decoder(QStringDecoder::Utf16LE);
    const QString decoded = decoder(utf16le);
    if (decoder.hasError() || decoded != QStringLiteral("中文😀"))
        throw std::runtime_error("Invalid UTF-16LE fixture");
    const auto transcoded = decoded.toUtf8().toStdString();
    compare({literal("中文"), literal("😀")}, {transcoded});
    const auto expected = std::string{1, 1, 1, 0};
    const Patterns overlapping{literal("a"), literal("aa"), literal("a"), literal("z")};
    for (const auto& name : backends())
        if (makeMatcher(name, overlapping)->scan({"aaa"}) != expected)
            throw std::runtime_error("Lost duplicate/overlapping pattern: " + name);

    std::mt19937 random{0x11a11U};
    std::vector<std::string> fuzz;
    for (int i = 0; i < 512; ++i) {
        std::string input(random() % 257, '\0');
        for (auto& ch : input) ch = static_cast<char>(random() & 255);
        fuzz.push_back(std::move(input));
    }
    const std::array<std::string, 8> atoms{"a", "b", "中文", "😀", "é", "Σ", std::string("\0", 1), "\xEF\xBB\xBF"};
    for (int i = 0; i < 512; ++i) {
        std::string input;
        for (unsigned n = random() % 80; n; --n) input += atoms[random() % atoms.size()];
        fuzz.push_back(std::move(input));
    }
    lines.clear();
    for (const auto& input : fuzz) lines.push_back(input);
    compare(basic, lines);
    std::cout << "Verified " << backends().size() << " backends: edge cases and 1024 seeded lines\n";
}
