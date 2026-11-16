#include "matcher.h"
#include <QByteArray>
#include <QStringDecoder>
#include <array>
#include <random>
#include <stdexcept>
#include <iostream>
#include <cstring>
#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

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
void verifyBoundaries()
{
    for (const size_t length : {1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
                               255, 256, 257, 511, 512, 513}) {
        std::string needle(length, 'a');
        needle.back() = 'z';
        const Patterns patterns{literal(QString::fromStdString(needle))};
        for (size_t offset = 0; offset < 64; ++offset) {
            const std::string storage = std::string(offset, 'x') + needle + std::string(64, 'x');
            const auto view = std::string_view(storage).substr(offset);
            compare(patterns, {view.substr(0, length), view.substr(0, length - 1),
                               view, view.substr(length), std::string_view(storage).substr(0, offset)});
        }
    }
}
#ifdef __linux__
class GuardedTail {
public:
    GuardedTail()
    {
        const auto size = sysconf(_SC_PAGESIZE);
        if (size <= 0) throw std::runtime_error("Cannot determine page size");
        page_ = static_cast<size_t>(size);
        data_ = static_cast<char*>(mmap(nullptr, page_ * 2, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (data_ == MAP_FAILED) throw std::runtime_error("Cannot allocate guarded fixture");
        if (mprotect(data_ + page_, page_, PROT_NONE) != 0) {
            munmap(data_, page_ * 2);
            throw std::runtime_error("Cannot protect fixture tail");
        }
    }
    ~GuardedTail() { munmap(data_, page_ * 2); }
    std::string_view line(size_t length, const std::string& needle, bool hit,
                          size_t tailGap = 0, size_t mismatch = std::string::npos)
    {
        if (length > page_) throw std::runtime_error("Guarded fixture too long");
        auto* start = data_ + page_ - length;
        if (length) std::memset(start, 'a', length);
        if (hit && length >= needle.size() + tailGap) {
            auto* match = data_ + page_ - tailGap - needle.size();
            std::memcpy(match, needle.data(), needle.size());
            if (mismatch < needle.size()) match[mismatch] = 'b';
        }
        return {start, length};
    }
private:
    char* data_;
    size_t page_;
};
void verifyGuardedTails()
{
    GuardedTail buffer;
    for (const size_t needleLength : {1, 2, 3, 4, 8, 9, 16, 24, 32, 33, 64, 65, 256, 257, 513}) {
        std::string needle(needleLength, 'a');
        needle.back() = 'z';
        const Patterns patterns{literal(QString::fromStdString(needle))};
        for (const size_t lineLength : {0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 128, 512, 1024}) {
            compare(patterns, {buffer.line(lineLength, needle, false)});
            for (size_t gap = 0; gap <= 32 && needleLength + gap <= lineLength; ++gap) {
                compare(patterns, {buffer.line(lineLength, needle, true, gap)});
                // Four distinct interior mutations include positions outside a three-byte SIMD filter.
                if (needleLength >= 9)
                    for (size_t mismatch = 1; mismatch <= 4; ++mismatch)
                        compare(patterns, {buffer.line(lineLength, needle, true, gap, mismatch)});
            }
        }
    }
}
#endif
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
    verifyBoundaries();
#ifdef __linux__
    verifyGuardedTails();
#endif
    std::cout << "Verified " << backends().size()
              << " backends: edge cases, SIMD boundaries and 1024 seeded lines"
#ifdef __linux__
              << ", including protected page tails"
#endif
              << '\n';
}
