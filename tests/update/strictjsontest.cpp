#include <QtTest>
#include "strictjson.h"
using namespace zzlogg::update::detail;
class StrictJsonTest : public QObject {
    Q_OBJECT
private slots:
    void rejects_data() {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("duplicate") << QByteArray(R"({"schema":1,"schema":1})");
        QTest::newRow("escaped-duplicate") << QByteArray(R"({"keyId":1,"\u006beyId":2})");
        QTest::newRow("nested-duplicate") << QByteArray(R"({"a":{"b":1,"b":2}})");
        QTest::newRow("deep") << QByteArray(17,'[') + "0" + QByteArray(17,']');
        QTest::newRow("trailing") << QByteArray("{} {}");
        QTest::newRow("bom") << QByteArray::fromHex("efbbbf") + "{}";
        QTest::newRow("utf8") << QByteArray("{\"a\":\"") + char(0xff) + "\"}";
        QTest::newRow("comment") << QByteArray("{/*x*/}");
        QTest::newRow("nul") << QByteArray("{}\0",3);
    }
    void rejects() {
        QFETCH(QByteArray,text);
        QVERIFY(!parseStrictJson({text.data(),size_t(text.size())},256*1024));
    }
    void validAndLimits() {
        const auto value = parseStrictJson(R"({"a":{"x":1},"b":{"x":2}})",128);
        QVERIFY(value);
        QCOMPARE((*value)["b"]["x"].get<int>(),2);
        QVERIFY(!parseStrictJson("{}",1));
        QVERIFY(parseStrictJson("{}",2));
        QVERIFY(parseStrictJson(std::string(16,'[')+"0"+std::string(16,']'),128));
    }
    void base64() {
        for (const auto& pair : std::vector<std::pair<std::string,std::string>>{
                {"",""},{"f","Zg=="},{"fo","Zm8="},{"foo","Zm9v"},
                {std::string("\0\xff",2),"AP8="}}) {
            QCOMPARE(encodeBase64(pair.first),pair.second);
            const auto decoded = decodeBase64(pair.second,128);
            QVERIFY(decoded);
            QCOMPARE(*decoded,pair.first);
        }
        for (const auto* invalid : {"Zg","Zg=","Zh==","Zm9=","Zg===","Zg==\n",
                                    "Zg==AAAA","====","Zm-v","Zg =","AA=A"}) {
            QVERIFY2(!decodeBase64(invalid,128),invalid);
        }
        QVERIFY(!decodeBase64("Zm9v",2));
        QVERIFY(decodeBase64("Zm9v",3));
    }
};
QTEST_GUILESS_MAIN(StrictJsonTest)
#include "strictjsontest.moc"
