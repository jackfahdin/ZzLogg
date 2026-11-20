#include "decompressor.h"
#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>

namespace {
QString fixture(const QString& name, const QString& suffix)
{
    return QString::fromUtf8(DECOMPRESS_FIXTURES) + '/' + name + '.' + suffix;
}
QByteArray payload(const QString& name)
{
    if (name == "empty") return {};
    auto line = QByteArray::fromHex(
        "31312d32302031323a30303a30302e303030202031323320203435362045204472697665723a206932632074696d656f757420e4b8ade696870d0a007461696c0a");
    if (name == "small") return line;
    constexpr qsizetype size = 4 * 1024 * 1024 + 73;
    return line.repeated((size + line.size() - 1) / line.size()).first(size);
}
struct Outcome {
    bool started, timely, result, signalledResult, closed;
    qsizetype completions;
};
Outcome run(const QString& path, QFile& output, AtomicFlag& interrupt)
{
    Decompressor decompressor;
    QSignalSpy finished(&decompressor, &Decompressor::finished);
    if (!decompressor.decompress(path, &output, interrupt)) return {};
    const bool timely = !finished.isEmpty() || finished.wait(2000);
    // Bound a broken read loop, then join before any referenced object is destroyed.
    if (!timely) interrupt.set();
    const bool result = decompressor.waitForResult();
    QCoreApplication::processEvents();
    return {true, timely, result, !finished.isEmpty() && finished.front().front().toBool(),
            !output.isOpen(), finished.size()};
}
class FaultFile : public QFile {
public:
    enum class Fault { ShortWrite, Throw, Fail, Cancel };
    FaultFile(const QString& path, Fault fault, AtomicFlag& interrupt)
        : QFile(path), fault_(fault), interrupt_(interrupt) {}
protected:
    qint64 writeData(const char* data, qint64 size) override
    {
        if (fault_ == Fault::Throw) throw std::runtime_error("injected output failure");
        if (fault_ == Fault::Fail) return -1;
        if (fault_ == Fault::Cancel) interrupt_.set();
        return QFile::writeData(data, fault_ == Fault::ShortWrite ? qMin<qint64>(size, 7) : size);
    }
private:
    Fault fault_;
    AtomicFlag& interrupt_;
};
}

class DecompressorTest : public QObject {
    Q_OBJECT
private:
    static void checkOutcome(const Outcome& actual, bool expected)
    {
        QVERIFY(actual.started);
        QVERIFY2(actual.timely, "Stream made no progress until the test cancelled it");
        QCOMPARE(actual.result, expected);
        QCOMPARE(actual.signalledResult, expected);
        QCOMPARE(actual.completions, qsizetype(1));
        QVERIFY(actual.closed);
    }
private Q_SLOTS:
    void valid_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("name");
        for (const auto* suffix : {"gz", "bz2", "xz"})
            for (const auto* name : {"empty", "small", "large"})
                QTest::newRow(qPrintable(QString(suffix) + '-' + name)) << QString(suffix) << QString(name);
    }
    void valid()
    {
        QFETCH(QString, suffix);
        QFETCH(QString, name);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile output(temporary.filePath("decoded.log"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        AtomicFlag interrupt;
        QCOMPARE(Decompressor::action(fixture(name, suffix)), DecompressAction::Decompress);
        checkOutcome(run(fixture(name, suffix), output, interrupt), true);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), payload(name));
    }
    void damaged_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<bool>("truncate");
        for (const auto* suffix : {"gz", "bz2", "xz"})
            for (bool truncate : {false, true})
                QTest::newRow(qPrintable(QString(suffix) + (truncate ? "-truncated" : "-corrupt")))
                    << QString(suffix) << truncate;
    }
    void damaged()
    {
        QFETCH(QString, suffix);
        QFETCH(bool, truncate);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile source(fixture("small", suffix));
        QVERIFY(source.open(QIODevice::ReadOnly));
        auto bytes = source.readAll();
        if (truncate) bytes.chop(8);
        else bytes[bytes.size() - (suffix == "gz" ? 8 : suffix == "xz" ? 16 : 3)] ^= 0x40;
        QFile damaged(temporary.filePath("damaged." + suffix));
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        QCOMPARE(damaged.write(bytes), bytes.size());
        damaged.close();
        QFile output(temporary.filePath("decoded.log"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        AtomicFlag interrupt;
        checkOutcome(run(damaged.fileName(), output, interrupt), false);
    }
    void outputFailures_data()
    {
        QTest::addColumn<int>("fault");
        QTest::newRow("short-write") << int(FaultFile::Fault::ShortWrite);
        QTest::newRow("exception") << int(FaultFile::Fault::Throw);
        QTest::newRow("write-error") << int(FaultFile::Fault::Fail);
        QTest::newRow("cancel-during-write") << int(FaultFile::Fault::Cancel);
    }
    void outputFailures()
    {
        QFETCH(int, fault);
        const auto mode = static_cast<FaultFile::Fault>(fault);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AtomicFlag interrupt;
        FaultFile output(temporary.filePath("decoded.log"), mode, interrupt);
        QVERIFY(output.open(mode == FaultFile::Fault::Fail ? QIODevice::WriteOnly
                                                          : QIODevice::WriteOnly | QIODevice::Unbuffered));
        const QString name = mode == FaultFile::Fault::Cancel ? "large" : "small";
        checkOutcome(run(fixture(name, "gz"), output, interrupt), false);
        if (mode == FaultFile::Fault::ShortWrite) QCOMPARE(QFileInfo(output.fileName()).size(), qint64(7));
        if (mode == FaultFile::Fault::Cancel) {
            QVERIFY(QFileInfo(output.fileName()).size() > 0);
            QVERIFY(QFileInfo(output.fileName()).size() < payload(name).size());
        }
    }
    void earlyFailure_data()
    {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancel-before-read") << true;
        QTest::newRow("input-open-fails") << false;
    }
    void earlyFailure()
    {
        QFETCH(bool, cancel);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile output(temporary.filePath("decoded.log"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        AtomicFlag interrupt(cancel);
        checkOutcome(run(cancel ? fixture("large", "gz") : temporary.filePath("missing.gz"), output, interrupt), false);
        QCOMPARE(QFileInfo(output.fileName()).size(), qint64(0));
    }
    void bufferedFlushFailure()
    {
        if (!QFileInfo::exists("/dev/full")) QSKIP("No native always-full device on this platform");
        QFile output("/dev/full");
        QVERIFY(output.open(QIODevice::WriteOnly));
        AtomicFlag interrupt;
        checkOutcome(run(fixture("small", "gz"), output, interrupt), false);
    }
};
QTEST_GUILESS_MAIN(DecompressorTest)
#include "decompressor_test.moc"
