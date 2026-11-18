#include "pipeline.h"
#include "booleanevaluator.h"
#include "regularexpression.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <stdexcept>
namespace pipeline {
Query Query::make(const QStringList& needles, const QString& op, bool inverse, bool sensitive, bool plain)
{
    if (needles.isEmpty() || (needles.size() > 1 && op.isEmpty()))
        throw std::runtime_error("Invalid probe query");
    Query q;
    q.inverse = inverse;
    QStringList quoted;
    for (const auto& needle : needles) {
        q.patterns.emplace_back(needle, sensitive, false, false, plain);
        auto escaped = needle;
        escaped.replace("\"", "\\\"");
        quoted.push_back("\"" + escaped + "\"");
        if (!q.booleanExpression.empty()) q.booleanExpression += " " + op.toStdString() + " ";
        q.booleanExpression += q.patterns.back().id();
    }
    q.publicPattern = RegularExpressionPattern{
        op.isEmpty() ? needles.front() : quoted.join(" " + op + " "),
        sensitive, inverse, !op.isEmpty(), plain};
    return q;
}
LogData::RawLines rawLines(const std::vector<std::string>& lines, const char* encoding,
                          const QString& prefilter, bool finalNewline)
{
    auto* codec = QTextCodec::codecForName(encoding);
    if (!codec) throw std::runtime_error("Unknown fixture encoding");
    LogData::RawLines raw;
    raw.textDecoder = TextCodecHolder(codec).makeDecoder();
    raw.prefilterPattern = QRegularExpression(prefilter);
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    for (size_t i = 0; i < lines.size(); ++i) {
        auto text = QByteArray(lines[i].data(), static_cast<qsizetype>(lines[i].size()));
        if (finalNewline || i + 1 < lines.size()) text += '\n';
        if (QByteArray(encoding) != "UTF-8") {
            const auto decoded = QString::fromUtf8(text);
            text = codec->fromUnicode(decoded.constData(), klogg::isize(decoded), &state);
        }
        raw.buffer.insert(raw.buffer.end(), text.begin(), text.end());
        raw.endOfLines.push_back(static_cast<qint64>(raw.buffer.size()));
    }
    return raw;
}
std::string Result::digest() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto line : matches) hash.addData(QByteArray::number(line) + ',');
    hash.addData(QByteArray::number(maxLength.get()) + ':' + QByteArray::number(processed)
                 + ':' + QByteArray::number(converted) + ':' + QByteArray::number(cancelled));
    return hash.result().toHex().toStdString();
}
struct Pipeline::Impl {
    Query query;
    size_t batch = 4096;
    std::unique_ptr<PatternMatcher> reference;
    std::unique_ptr<LiteralMatcher> candidate;
    std::unique_ptr<BooleanExpressionEvaluator> evaluator;
    Impl(const Query& q, const std::string& backend) : query(q)
    {
        if (backend != "qt" && backend != "memchr-line" && backend != "memchr-128"
            && backend != "memchr-4096") throw std::runtime_error("Unknown pipeline backend");
        RegularExpression expression(q.publicPattern);
        if (!expression.isValid()) throw std::runtime_error(expression.errorString().toStdString());
        if (backend == "qt") reference = expression.createMatcher();
        else {
            batch = backend == "memchr-line" ? 1 : backend == "memchr-128" ? 128 : 4096;
            candidate = makeMatcher("memchr", q.patterns);
            if (q.publicPattern.isBoolean) {
                evaluator = std::make_unique<BooleanExpressionEvaluator>(q.booleanExpression, q.patterns);
                if (!evaluator->isValid()) throw std::runtime_error(evaluator->errorString());
            }
        }
    }
};
Pipeline::Pipeline(const Query& q, const std::string& backend) : impl_(std::make_unique<Impl>(q, backend)) {}
Pipeline::~Pipeline() = default;
Result Pipeline::run(LogData::RawLines& raw, RunOptions options)
{
    Result result;
    if (options.cancelled) { result.cancelled = true; return result; }
    QElapsedTimer conversion;
    if (options.profile) conversion.start();
    const auto views = raw.buildUtf8View();
    result.converted = views.size();
    if (options.profile) result.conversionNs = conversion.nsecsElapsed();
    bool cancelled = false;
    for (size_t start = 0; start < views.size(); start += impl_->batch) {
        if (cancelled) break;
        QElapsedTimer timer;
        if (options.profile) timer.start();
        if (start == 0 && options.requestDuringFirstBatch) cancelled = true;
        const auto end = std::min(views.size(), start + impl_->batch);
        std::string bitmap;
        if (impl_->candidate) {
            const Lines lines(views.begin() + static_cast<ptrdiff_t>(start),
                              views.begin() + static_cast<ptrdiff_t>(end));
            bitmap = impl_->candidate->scan(lines);
            result.maxBitmapBytes = std::max(result.maxBitmapBytes, bitmap.size());
        }
        for (size_t row = start; row < end; ++row) {
            bool match;
            if (impl_->reference) match = impl_->reference->hasMatch(views[row]);
            else {
                const auto count = impl_->query.patterns.size();
                const auto cells = std::string_view(bitmap).substr((row - start) * count, count);
                match = impl_->evaluator ? impl_->evaluator->evaluate(cells) : cells.front() != 0;
                if (impl_->query.inverse) match = !match;
            }
            if (match) {
                result.matches.add(static_cast<uint64_t>(row));
                result.maxLength = qMax(result.maxLength, getUntabifiedLength(views[row]));
            }
            ++result.processed;
        }
        if (options.profile) result.maxBatchNs = std::max(result.maxBatchNs, timer.nsecsElapsed());
    }
    result.cancelled = cancelled;
    return result;
}
}
