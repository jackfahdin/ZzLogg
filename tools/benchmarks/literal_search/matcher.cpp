#include "matcher.h"
#include "hsregularexpression.h"
#include <QByteArrayView>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
struct ByteView {
    const char* data;
    size_t size;
    unsigned char eligible;
};
#ifdef LITERAL_ENABLE_RUST
extern "C" {
void* literal_new(const ByteView*, size_t, bool);
void literal_free(void*);
void literal_scan(const void*, const ByteView*, size_t, char*);
size_t literal_memory(const void*);
}
#endif
bool eligible(const Patterns& patterns)
{
    return !patterns.empty() && std::all_of(patterns.begin(), patterns.end(), [](const auto& p) {
        return p.isPlainText && p.isCaseSensitive && !p.pattern.isEmpty() && p.pattern.isValidUtf16();
    });
}
bool byteEligible(std::string_view line)
{
    // QString::fromUtf8's BOM handling and malformed-byte replacement stay authoritative.
    return !(line.size() >= 3 && line.substr(0, 3) == "\xEF\xBB\xBF")
           && QByteArrayView(line.data(), static_cast<qsizetype>(line.size())).isValidUtf8();
}
class QtBaseline final : public LiteralMatcher {
public:
    explicit QtBaseline(const Patterns& patterns) : matcher_(patterns), count_(patterns.size()) {}
    std::string scan(const Lines& lines) const override
    {
        std::string result(lines.size() * count_, 0);
        if (count_ == 0) return result;
        for (size_t row = 0; row < lines.size(); ++row) {
            const auto actual = matcher_.match(lines[row]);
            std::copy(actual.begin(), actual.end(), result.data() + row * count_);
        }
        return result;
    }
private:
    DefaultRegularExpressionMatcher matcher_;
    size_t count_;
};
class Matcher final : public LiteralMatcher {
public:
    Matcher(std::string backend, const Patterns& patterns)
        : backend_(std::move(backend)), fallback_(patterns), fast_(eligible(patterns))
    {
        for (const auto& p : patterns) {
            text_.push_back(p.pattern);
            bytes_.push_back(p.pattern.toUtf8().toStdString());
        }
#ifdef LITERAL_ENABLE_RUST
        if (fast_ && (backend_ == "memchr" || backend_ == "aho")) {
            std::vector<ByteView> views;
            for (const auto& needle : bytes_) views.push_back({needle.data(), needle.size(), 1});
            rust_ = literal_new(views.data(), views.size(), backend_ == "aho");
            if (!rust_) throw std::runtime_error("Rust matcher construction failed");
        }
#endif
    }
    ~Matcher() override
    {
#ifdef LITERAL_ENABLE_RUST
        if (rust_) literal_free(rust_);
#endif
    }
    size_t automatonBytes() const override
    {
#ifdef LITERAL_ENABLE_RUST
        return rust_ ? literal_memory(rust_) : 0;
#else
        return 0;
#endif
    }
    std::string scan(const Lines& lines) const override
    {
        const auto count = text_.size();
        std::string result(lines.size() * count, 0);
        if (count == 0 || lines.empty()) return result;
        std::vector<ByteView> views;
#ifdef LITERAL_ENABLE_RUST
        if (rust_) views.reserve(lines.size());
#endif
        for (size_t row = 0; row < lines.size(); ++row) {
            const auto line = lines[row];
            auto* out = result.data() + row * count;
            const bool useFast = fast_
                && (backend_ == "qstring" || byteEligible(line));
            if (!useFast) {
                const auto actual = fallback_.match(line);
                std::copy(actual.begin(), actual.end(), out);
            }
#ifdef LITERAL_ENABLE_RUST
            if (rust_) {
                views.push_back({line.data(), line.size(), static_cast<unsigned char>(useFast)});
                continue;
            }
#endif
            if (!useFast) continue;
            if (backend_ == "qstring") {
                const auto decoded = QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
                for (size_t i = 0; i < count; ++i) out[i] = decoded.contains(text_[i]);
            } else {
                for (size_t i = 0; i < count; ++i) out[i] = line.find(bytes_[i]) != line.npos;
            }
        }
#ifdef LITERAL_ENABLE_RUST
        // Exactly one FFI call per batch, never one call per line or pattern.
        if (rust_) literal_scan(rust_, views.data(), views.size(), result.data());
#endif
        return result;
    }
private:
    std::string backend_;
    DefaultRegularExpressionMatcher fallback_;
    bool fast_;
    std::vector<QString> text_;
    std::vector<std::string> bytes_;
#ifdef LITERAL_ENABLE_RUST
    void* rust_ = nullptr;
#endif
};
}

std::vector<std::string> backends()
{
    std::vector<std::string> names{"qt", "qstring", "std"};
#ifdef LITERAL_ENABLE_RUST
    names.insert(names.end(), {"memchr", "aho"});
#endif
    return names;
}
std::unique_ptr<LiteralMatcher> makeMatcher(const std::string& name, const Patterns& patterns)
{
    const auto available = backends();
    if (std::find(available.begin(), available.end(), name) == available.end())
        throw std::runtime_error("Unknown backend: " + name);
    if (name == "qt") return std::make_unique<QtBaseline>(patterns);
    return std::make_unique<Matcher>(name, patterns);
}
