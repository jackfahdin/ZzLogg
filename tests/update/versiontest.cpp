#include "zzlogg/update/version.h"
#include <QtTest>
using namespace zzlogg::update;

class VersionTest : public QObject {
    Q_OBJECT
private slots:
    void valid_data() {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<unsigned>("year");
        QTest::addColumn<unsigned>("month");
        QTest::addColumn<unsigned>("patch");
        QTest::newRow("current") << QByteArray("26.09.00") << 26u << 9u << 0u;
        QTest::newRow("minimum") << QByteArray("00.01.00") << 0u << 1u << 0u;
        QTest::newRow("maximum") << QByteArray("99.12.99") << 99u << 12u << 99u;
    }
    void valid() {
        QFETCH(QByteArray, text); QFETCH(unsigned, year);
        QFETCH(unsigned, month); QFETCH(unsigned, patch);
        const auto v = parseVersion({text.constData(), size_t(text.size())});
        QVERIFY(v);
        QCOMPARE(v->year, year); QCOMPARE(v->month, month); QCOMPARE(v->patch, patch);
    }
    void invalid_data() {
        QTest::addColumn<QByteArray>("text");
        for (const char* s : {"", "26.9.00", "26.09.0", "026.09.00", "26.00.00",
             "26.13.00", "26.09.100", "26.09.00-dev", " 26.09.00",
             "26.09.00\n", "26-09-00", "+6.09.00", "aa.09.00", "26.09.-1"}) {
            QTest::newRow(s) << QByteArray(s);
        }
        QTest::newRow("full-width") << QStringLiteral("２６.０９.００").toUtf8();
        QTest::newRow("embedded-null") << QByteArray("26.09.\0\0", 8);
    }
    void invalid() {
        QFETCH(QByteArray, text);
        QVERIFY(!parseVersion({text.constData(), size_t(text.size())}));
    }
    void ordering() {
        auto check = [](const char* a, const char* b, int expected) {
            auto left = parseVersion(a), right = parseVersion(b);
            QVERIFY(left && right);
            QCOMPARE(compareVersion(*left, *right), expected);
            QCOMPARE(compareVersion(*right, *left), -expected);
        };
        check("26.09.99", "26.10.00", -1);
        check("26.12.99", "27.01.00", -1);
        check("26.09.01", "26.09.00", 1);
        check("26.09.00", "26.09.00", 0);
    }
};
QTEST_GUILESS_MAIN(VersionTest)
#include "versiontest.moc"
