#pragma once
#include "logdata.h"
#include "logfiltereddataworker.h"
#include "experimental.h"
#include <QByteArray>
#include <QTemporaryFile>
#include <vector>
namespace workerprobe {
void require(bool good, const char* message);
class Fixture {
public:
    explicit Fixture(const QByteArray& bytes, const char* codec = "UTF-8");
    void reload(const char* codec = "UTF-8");
    QTemporaryFile file;
    LogData log;
};
struct Outcome {
    SearchResults result;
    std::vector<int> progress;
    qint64 elapsedNs = 0, cancelNs = 0;
    bool cancelRequested = false;
    experimental::Metrics diagnostics;
    std::string digest() const;
};
Outcome search(Fixture&, const RegularExpressionPattern&, bool memchr, unsigned threads = 1,
               unsigned chunk = 4096, bool cancel = false,
               uint64_t start = 0, uint64_t end = std::numeric_limits<uint64_t>::max());
void verify();
}
