#include <kcompressiondevice.h>
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {
QByteArray fixture(const QString& name)
{
    QFile file(QString::fromUtf8(STREAM_FIXTURES) + '/' + name);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read independent stream fixture");
    return file.readAll();
}
KCompressionDevice::CompressionType type(const QString& suffix)
{
    return suffix == "bz2" ? KCompressionDevice::BZip2
         : suffix == "gz" ? KCompressionDevice::GZip : KCompressionDevice::Xz;
}
// A real byte source with controlled read boundaries and an optional I/O error.
class ChunkSource : public QBuffer {
public:
    ChunkSource(QByteArray bytes, qint64 chunk) : chunk_(chunk) { setData(bytes); }
    qint64 failAfter = -1;
protected:
    qint64 readData(char* data, qint64 size) override
    {
        if (failAfter >= 0 && pos() >= failAfter) {
            setErrorString("Injected input read failure");
            return -1;
        }
        return QBuffer::readData(data, qMin(size, chunk_));
    }
private:
    qint64 chunk_;
};
}

class CompressionDeviceTest : public QObject {
    Q_OBJECT
    static void compareAll(KCompressionDevice& device, const QByteArray& expected, int readSize)
    {
        QByteArray actual;
        QByteArray chunk(readSize, '\0');
        // Bounded even if an EOF regression returns zero forever.
        for (int attempts = 0; attempts < 100000; ++attempts) {
            const auto count = device.read(chunk.data(), chunk.size());
            QVERIFY(count >= 0);
            if (count == 0) {
                QVERIFY(device.atEnd());
                QCOMPARE(actual, expected);
                return;
            }
            actual.append(chunk.constData(), count);
            QVERIFY(actual.size() <= expected.size());
        }
        QFAIL("Decoder did not reach EOF");
    }
private Q_SLOTS:
    void boundaries_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("inputSize");
        QTest::addColumn<int>("outputSize");
        for (const auto* suffix : {"gz", "bz2", "xz"}) {
            int firstSize = 0;
            for (const auto& value : QJsonDocument::fromJson(fixture("manifest.json")).array()) {
                const auto row = value.toObject();
                if (row["file"].toString() == QString("two.") + suffix)
                    firstSize = row["first_size"].toInt();
            }
            QVERIFY(firstSize > 10);
            for (int input : {8192, firstSize - 1, firstSize, firstSize + 1})
                for (int output : {1, 16384})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(suffix).arg(input).arg(output)))
                        << QString(suffix) << QString("two") << input << output;
            for (int input : {1, 5, 9})
                QTest::newRow(qPrintable(QString("%1-short-input-%2").arg(suffix).arg(input)))
                    << QString(suffix) << QString("two") << input << 6;
            QTest::newRow(qPrintable(QString(suffix) + "-full-output"))
                << QString(suffix) << QString("output-boundary") << 8192 << 16384;
        }
    }
    void boundaries()
    {
        QFETCH(QString, suffix); QFETCH(QString, name);
        QFETCH(int, inputSize); QFETCH(int, outputSize);
        const auto bytes = fixture(name + '.' + suffix);
        ChunkSource source(bytes, inputSize);
        QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
        KCompressionDevice device(&source, false, type(suffix));
        QVERIFY(device.open(QIODevice::ReadOnly));
        const auto expected = fixture(name == "two" ? "first.bin" : "boundary.bin") + fixture("second.bin");
        compareAll(device, expected, outputSize);
        QVERIFY(device.seek(0));
        QCOMPARE(device.read(3), expected.first(3));
        QVERIFY(device.seek(0));
        compareAll(device, expected, outputSize);
        device.close();
        QVERIFY(source.seek(0));
        QVERIFY(device.open(QIODevice::ReadOnly));
        compareAll(device, expected, outputSize);
    }
    void gzipPlainText_data()
    {
        QTest::addColumn<int>("inputSize");
        for (int size : {1, 5, 9, 8192})
            QTest::newRow(qPrintable(QString::number(size))) << size;
    }
    void gzipPlainText()
    {
        QFETCH(int, inputSize);
        for (const QByteArray bytes : {QByteArray("plain"), QByteArray("uncompressed text\n")}) {
            ChunkSource source(bytes, inputSize);
            QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
            KCompressionDevice device(&source, false, KCompressionDevice::GZip);
            QVERIFY(device.open(QIODevice::ReadOnly));
            compareAll(device, bytes, 1);
        }
    }
    void inputFailure_data()
    {
        QTest::addColumn<QString>("suffix");
        for (const auto* suffix : {"gz", "bz2", "xz"})
            QTest::newRow(suffix) << QString(suffix);
    }
    void inputFailure()
    {
        QFETCH(QString, suffix);
        ChunkSource source(fixture("large-first." + suffix), 8192);
        source.failAfter = 8192;
        QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
        KCompressionDevice device(&source, false, type(suffix));
        QVERIFY(device.open(QIODevice::ReadOnly));
        QByteArray chunk(32768, '\0');
        bool failed = false;
        for (int i = 0; i < 8; ++i) {
            if (device.read(chunk.data(), chunk.size()) < 0) { failed = true; break; }
        }
        QVERIFY(failed);
        QVERIFY(!device.atEnd());
        device.close();
        source.failAfter = -1;
        QVERIFY(source.seek(0));
        QVERIFY(device.open(QIODevice::ReadOnly));
        compareAll(device, fixture("large.bin") + fixture("second.bin"), 32768);
    }
};
QTEST_GUILESS_MAIN(CompressionDeviceTest)
#include "compression_device_test.moc"
