#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "zzlogg/update/signature.h"

using zzlogg::update::verifyEd25519;

class SignatureTest : public QObject {
    Q_OBJECT
private slots:
    void vectors_data()
    {
        QTest::addColumn<QByteArray>("key");
        QTest::addColumn<QByteArray>("message");
        QTest::addColumn<QByteArray>("signature");
        QFile file(QString::fromUtf8(UPDATE_FIXTURE_DIR) + "/rfc8032.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(document.array().size(), 2);
        for (const auto& value : document.array()) {
            const auto row = value.toObject();
            const auto key = QByteArray::fromHex(row["publicKey"].toString().toLatin1());
            const auto signature = QByteArray::fromHex(row["signature"].toString().toLatin1());
            QCOMPARE(key.size(), 32);
            QCOMPARE(signature.size(), 64);
            QTest::newRow(qPrintable(row["name"].toString()))
                << key << QByteArray::fromHex(row["message"].toString().toLatin1()) << signature;
        }
    }

    // Detects a wrong algorithm, argument order, or success return convention.
    void vectors()
    {
        QFETCH(QByteArray, key);
        QFETCH(QByteArray, message);
        QFETCH(QByteArray, signature);
        QVERIFY(verifyEd25519({message.constData(), size_t(message.size())},
                             {key.begin(), key.end()}, {signature.begin(), signature.end()}));
    }

    void tampering_data() { vectors_data(); }

    // Each input independently influences acceptance; malformed buffers must not reach C.
    void tampering()
    {
        QFETCH(QByteArray, key);
        QFETCH(QByteArray, message);
        QFETCH(QByteArray, signature);
        const std::vector<std::uint8_t> publicKey(key.begin(), key.end());
        const std::vector<std::uint8_t> validSignature(signature.begin(), signature.end());
        const std::string_view bytes(message.constData(), size_t(message.size()));
        auto changedKey = publicKey;
        changedKey[0] ^= 1;
        QVERIFY(!verifyEd25519(bytes, changedKey, validSignature));
        auto changedSignature = validSignature;
        changedSignature[0] ^= 1;
        QVERIFY(!verifyEd25519(bytes, publicKey, changedSignature));
        if (!message.isEmpty()) {
            auto changedMessage = message;
            changedMessage[0] = char(changedMessage.at(0) ^ 1);
            QVERIFY(!verifyEd25519(
                {changedMessage.constData(), size_t(changedMessage.size())},
                publicKey, validSignature));
        } else {
            QVERIFY(verifyEd25519({}, publicKey, validSignature));
        }
        message.append(char(0));
        QVERIFY(!verifyEd25519({message.constData(), size_t(message.size())}, publicKey, validSignature));
        // Reconstruct the original view after QByteArray's potential reallocation.
        const std::string_view original(message.constData(), size_t(message.size() - 1));
        for (const size_t length : {size_t(0), size_t(1), size_t(31), size_t(33), size_t(64)})
            QVERIFY(!verifyEd25519(original, std::vector<std::uint8_t>(length), validSignature));
        for (const size_t length : {size_t(0), size_t(1), size_t(32), size_t(63), size_t(65)})
            QVERIFY(!verifyEd25519(original, publicKey, std::vector<std::uint8_t>(length)));
        changedSignature = validSignature;
        changedSignature.pop_back();
        QVERIFY(!verifyEd25519(original, publicKey, changedSignature));
        QVERIFY(!verifyEd25519(original, publicKey, std::vector<std::uint8_t>(64)));
    }
};
QTEST_GUILESS_MAIN(SignatureTest)
#include "signaturetest.moc"
