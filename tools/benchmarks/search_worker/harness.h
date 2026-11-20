#pragma once
#include "logdata.h"
#include "logfiltereddataworker.h"
#include "experimental.h"
#include <QByteArray>
#include <QTemporaryFile>
#include <vector>
namespace workerprobe {
void require(bool good, const char* message);
struct FileSource { QString path; };
class Fixture {
public:
    explicit Fixture(const QByteArray& bytes, const char* codec = "UTF-8");
    Fixture(FileSource source, const char* codec, int timeoutMs = 30000);
    void reload(const char* codec = "UTF-8");
    QByteArray fileHash;
    QTemporaryFile file;
    LogData log;
private:
    void initialize(const char* codec);
    int timeoutMs_ = 30000;
};
struct Outcome {
    SearchResults result;
    std::vector<int> progress;
    qint64 elapsedNs = 0, cancelNs = 0, cpuNs = -1;
    qint64 userCpuNs = -1, systemCpuNs = -1;
    qint64 voluntarySwitches = -1, involuntarySwitches = -1;
    qint64 firstProgressNs = -1, firstHitNs = -1;
    bool cancelRequested = false;
    experimental::Metrics diagnostics;
    std::string digest() const;
};
Outcome search(Fixture&, const RegularExpressionPattern&, bool memchr, unsigned threads = 1,
               unsigned chunk = 4096, bool cancel = false,
               uint64_t start = 0, uint64_t end = std::numeric_limits<uint64_t>::max(),
               int timeoutMs = 30000);
void verify();
void verifyRecovery(const QString& mode, bool wake);
void verifyIntegration(bool requireAccelerated);
}
