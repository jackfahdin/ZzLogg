#pragma once
#include <QTextCodec>
#include <QStringList>
#include "logdata.h"
#include "logfiltereddataworker.h"
#include "matcher.h"

namespace pipeline {
struct Query {
    Patterns patterns;
    RegularExpressionPattern publicPattern;
    std::string booleanExpression;
    bool inverse = false;
    static Query make(const QStringList& needles, const QString& op = {},
                      bool inverse = false, bool sensitive = true, bool plain = true);
};
struct RunOptions {
    bool profile = false;
    bool cancelled = false;
    // Deterministic request immediately AFTER the first batch's boundary check.
    bool requestDuringFirstBatch = false;
};
struct Result {
    SearchResultArray matches;
    LineLength maxLength{0};
    size_t processed = 0;
    size_t converted = 0;
    bool cancelled = false;
    qint64 conversionNs = 0;
    qint64 maxBatchNs = 0;
    size_t maxBitmapBytes = 0;
    std::string digest() const;
};
class Pipeline {
public:
    Pipeline(const Query&, const std::string& backend);
    ~Pipeline();
    Result run(LogData::RawLines&, RunOptions = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
LogData::RawLines rawLines(const std::vector<std::string>& lines, const char* encoding = "UTF-8",
                          const QString& prefilter = {}, bool finalNewline = true);
void verify();
}
