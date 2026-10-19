#include "legacystorage.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

QString normalized( const QString& path )
{
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool writeFile( const QString& path )
{
    QDir{}.mkpath( QFileInfo{ path }.absolutePath() );
    QSaveFile file{ path };
    const QByteArray contents{ "[General]\nfixture=true\n" };
    return file.open( QIODevice::WriteOnly ) && file.write( contents ) == contents.size()
           && file.commit();
}

} // namespace

class LegacyStorageTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void portableConfigTakesPriorityOverUserFiles();
    void userConfigOrSessionAloneIsDetected_data();
    void userConfigOrSessionAloneIsDetected();
    void returnsNothingWhenNoLegacyIniExists();
};

void LegacyStorageTest::portableConfigTakesPriorityOverUserFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "app" ) );
    const QString userDirectory = temporaryDirectory.filePath( QStringLiteral( "user" ) );
    QVERIFY(
        writeFile( QDir{ applicationDirectory }.filePath( QStringLiteral( "ZzLogg.conf" ) ) ) );
    QVERIFY( writeFile(
        QDir{ applicationDirectory }.filePath( QStringLiteral( "ZzLogg_session.conf" ) ) ) );
    QVERIFY( writeFile( QDir{ userDirectory }.filePath( QStringLiteral( "ZzLogg.ini" ) ) ) );
    QVERIFY(
        writeFile( QDir{ userDirectory }.filePath( QStringLiteral( "ZzLogg_session.ini" ) ) ) );

    const auto detected
        = LegacyStorageDetector::detect( applicationDirectory, userDirectory );

    QVERIFY( detected.has_value() );
    QCOMPARE( detected->mode, StorageMode::ProgramDirectory );
    QCOMPARE( detected->configFile,
              normalized( applicationDirectory + QStringLiteral( "/ZzLogg.conf" ) ) );
    QCOMPARE( detected->sessionFile,
              normalized( applicationDirectory + QStringLiteral( "/ZzLogg_session.conf" ) ) );
}

void LegacyStorageTest::userConfigOrSessionAloneIsDetected_data()
{
    QTest::addColumn<QString>( "existingName" );
    QTest::addColumn<QString>( "expectedSessionName" );
    QTest::newRow( "config-only" ) << QStringLiteral( "ZzLogg.ini" )
                                         << QStringLiteral( "ZzLogg.ini" );
    QTest::newRow( "session-only" ) << QStringLiteral( "ZzLogg_session.ini" )
                                          << QStringLiteral( "ZzLogg_session.ini" );
}

void LegacyStorageTest::userConfigOrSessionAloneIsDetected()
{
    QFETCH( QString, existingName );
    QFETCH( QString, expectedSessionName );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "app" ) );
    const QString userDirectory = temporaryDirectory.filePath( QStringLiteral( "user" ) );
    QVERIFY( writeFile( QDir{ userDirectory }.filePath( existingName ) ) );

    const auto detected
        = LegacyStorageDetector::detect( applicationDirectory, userDirectory );

    QVERIFY( detected.has_value() );
    QCOMPARE( detected->mode, StorageMode::UserDirectory );
    QCOMPARE( detected->configFile, normalized( userDirectory + QStringLiteral( "/ZzLogg.ini" ) ) );
    QCOMPARE( detected->sessionFile,
              normalized( QDir{ userDirectory }.filePath( expectedSessionName ) ) );
}

void LegacyStorageTest::returnsNothingWhenNoLegacyIniExists()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );

    const auto detected = LegacyStorageDetector::detect(
        temporaryDirectory.filePath( QStringLiteral( "app" ) ),
        temporaryDirectory.filePath( QStringLiteral( "user" ) ) );

    QVERIFY( !detected.has_value() );
}

QTEST_MAIN( LegacyStorageTest )
#include "legacystoragetest.moc"
