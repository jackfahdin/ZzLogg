#include "decompressor.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <functional>
#include <cstdio>
#include <stdexcept>

namespace {
QString fixture(const QString& name) { return QString::fromUtf8(ARCHIVE_FIXTURES) + '/' + name; }
QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read fixture/output");
    return file.readAll();
}
struct Outcome { bool started, timely, result, signalled; qsizetype completions; };
Outcome run(Decompressor& worker, AtomicFlag& cancel, const std::function<bool()>& start)
{
    QSignalSpy finished(&worker, &Decompressor::finished);
    if (!start()) return {};
    const bool timely = !finished.isEmpty() || finished.wait(2000);
    if (!timely) cancel.set();
    const bool result = worker.waitForResult();
    QCoreApplication::processEvents();
    return {true, timely, result, !finished.isEmpty() && finished.front().front().toBool(), finished.size()};
}
}

class ArchiveTest : public QObject {
    Q_OBJECT
    static void check(const Outcome& outcome, bool expected)
    {
        QVERIFY(outcome.started);
        QVERIFY(outcome.timely);
        QCOMPARE(outcome.result, expected);
        QCOMPARE(outcome.signalled, expected);
        QCOMPARE(outcome.completions, qsizetype(1));
    }
private Q_SLOTS:
    void extractsIndependentFiles_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto* name : {"stored.zip", "deflated.zip", "logs.tar", "logs.tar.gz", "logs.tar.bz2",
                                 "logs.tar.xz", "solid-on.7z", "solid-off.7z"})
            QTest::newRow(name) << QString(name);
    }
    void extractsIndependentFiles()
    {
        QFETCH(QString, name);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto dest = temporary.filePath("result");
        Decompressor worker;
        AtomicFlag cancel;
        QCOMPARE(Decompressor::action(fixture(name)), DecompressAction::Extract);
        check(run(worker, cancel, [&] { return worker.extract(fixture(name), dest, cancel); }), true);
        const auto files = QJsonDocument::fromJson(read(fixture("manifest.json"))).object()["files"].toObject();
        QStringList actual;
        QDirIterator it(dest, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) actual.append(QDir(dest).relativeFilePath(it.next()));
        actual.sort();
        auto expected = files.keys(); expected.sort();
        QCOMPARE(actual, expected);
        for (const auto& member : expected)
            QCOMPARE(read(dest + '/' + member), QByteArray::fromHex(files[member].toObject()["hex"].toString().toLatin1()));
    }
    void refusesIncompleteMember()
    {
        auto bytes = read(fixture("deflated.zip"));
        const auto central = bytes.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central >= 0);
        const auto size = qFromLittleEndian<quint32>(bytes.constData() + central + 24);
        qToLittleEndian<quint32>(size + 19, bytes.data() + central + 24);
        qToLittleEndian<quint32>(size + 19, bytes.data() + 22);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile zip(temporary.filePath("incomplete.zip"));
        QVERIFY(zip.open(QIODevice::WriteOnly)); QCOMPARE(zip.write(bytes), bytes.size()); zip.close();
        Decompressor worker;
        AtomicFlag cancel;
        check(run(worker, cancel, [&] { return worker.extract(zip.fileName(), temporary.filePath("result"), cancel); }), false);
    }
    void invalidAndCancelled_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("cancelled");
        for (const auto* name : {"deflated.zip", "logs.tar", "solid-on.7z"}) {
            QTest::newRow(qPrintable(QString(name) + "-truncated")) << QString(name) << false;
            QTest::newRow(qPrintable(QString(name) + "-cancelled")) << QString(name) << true;
        }
    }
    void invalidAndCancelled()
    {
        QFETCH(QString, name);
        QFETCH(bool, cancelled);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile source(temporary.filePath(name));
        QVERIFY(source.open(QIODevice::WriteOnly));
        const auto bytes = read(fixture(name));
        const auto content = cancelled ? bytes : bytes.first(8);
        QCOMPARE(source.write(content), content.size()); source.close();
        Decompressor worker;
        AtomicFlag cancel(cancelled);
        check(run(worker, cancel, [&] { return worker.extract(source.fileName(), temporary.filePath("result"), cancel); }), false);
    }
    void outputWriteFailure()
    {
        if (!QFileInfo::exists("/dev/full")) QSKIP("No native always-full device");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto dest = temporary.filePath("result");
        QVERIFY(QDir().mkpath(dest + "/logs"));
        QVERIFY(QFile::link("/dev/full", dest + "/logs/dmesg.log"));
        Decompressor worker;
        AtomicFlag cancel;
        check(run(worker, cancel, [&] { return worker.extract(fixture("stored.zip"), dest, cancel); }), false);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--stream-probe") {
        QFile output(args[3]);
        if (!output.open(QIODevice::WriteOnly)) return 2;
        AtomicFlag cancel;
        Decompressor worker;
        const auto outcome = run(worker, cancel, [&] { return worker.decompress(args[2], &output, cancel); });
        output.close();
        const auto bytes = read(args[3]);
        const QJsonObject row{{"started", outcome.started}, {"timely", outcome.timely}, {"success", outcome.result},
            {"signal_success", outcome.signalled}, {"signals", int(outcome.completions)}, {"bytes", double(bytes.size())},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
        const auto json = QJsonDocument(row).toJson(QJsonDocument::Compact);
        puts(json.constData());
        return 0;
    }
    ArchiveTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "archive_test.moc"
