#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <iostream>
#include <memory>
#include <sstream>

#include "logger.h"
#include "storagecontext.h"

class StorageConsumersTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void storesLogsInsideTheInstalledDataRoot();
    void keepsConsoleLoggingWhenTheLogFileCannotBeOpened();

  private:
    std::unique_ptr<QTemporaryDir> temporaryRoot_;
};

void StorageConsumersTest::initTestCase()
{
    temporaryRoot_ = std::make_unique<QTemporaryDir>();
    QVERIFY( temporaryRoot_->isValid() );

    QString error;
    const StorageLocation location{ StorageMode::CustomDirectory, temporaryRoot_->path(),
                                    temporaryRoot_->filePath( "bootstrap/storage.ini" ), true };
    QVERIFY2( StorageContext::install( location, &error ), qPrintable( error ) );
    QVERIFY2( StorageContext::current().ensureDirectories( &error ), qPrintable( error ) );
}

void StorageConsumersTest::cleanupTestCase()
{
    logging::enableFileLogging( false );
}

void StorageConsumersTest::storesLogsInsideTheInstalledDataRoot()
{
    const auto& context = StorageContext::current();

    QVERIFY( logging::currentLogFilePath().isEmpty() );
    logging::enableFileLogging( true, logging::LogLevel::Debug );

    const auto logFilePath = QDir::cleanPath( logging::currentLogFilePath() );
    const auto logsDirectory = QDir::cleanPath( context.logsDirectory() );
    QVERIFY( QFileInfo{ logFilePath }.isAbsolute() );
    QVERIFY( logFilePath.startsWith( logsDirectory + QLatin1Char( '/' ) ) );
    const auto logFileName = QFileInfo{ logFilePath }.fileName();
    QVERIFY( logFileName.startsWith( QStringLiteral( "ZzLogg_" ) ) );
    QVERIFY( logFileName.endsWith(
        QStringLiteral( "_%1.log" ).arg( QCoreApplication::applicationPid() ) ) );

    logging::enableFileLogging( true, logging::LogLevel::Info );
    QCOMPARE( QDir::cleanPath( logging::currentLogFilePath() ), logFilePath );

    const auto message = QStringLiteral( "storage consumer integration marker" );
    qInfo().noquote() << message;
    logging::enableFileLogging( false );

    QVERIFY( logging::currentLogFilePath().isEmpty() );
    QVERIFY( QFileInfo::exists( logFilePath ) );
    QFile logFile{ logFilePath };
    QVERIFY( logFile.open( QIODevice::ReadOnly ) );
    QVERIFY( QString::fromUtf8( logFile.readAll() ).contains( message ) );

}

void StorageConsumersTest::keepsConsoleLoggingWhenTheLogFileCannotBeOpened()
{
    const auto logsDirectory = StorageContext::current().logsDirectory();
    QVERIFY( QDir{ logsDirectory }.removeRecursively() );
    QFile directoryBlocker{ logsDirectory };
    QVERIFY( directoryBlocker.open( QIODevice::WriteOnly ) );
    directoryBlocker.close();

    std::ostringstream errorOutput;
    auto* const previousErrorBuffer = std::cerr.rdbuf( errorOutput.rdbuf() );
    logging::enableFileLogging( true );
    std::cerr.rdbuf( previousErrorBuffer );

    QVERIFY( logging::currentLogFilePath().isEmpty() );
    QVERIFY(
        QString::fromLocal8Bit( errorOutput.str() ).contains( QDir::cleanPath( logsDirectory ) ) );

    std::ostringstream consoleOutput;
    auto* const previousConsoleBuffer = std::cout.rdbuf( consoleOutput.rdbuf() );
    qInfo().noquote() << "console fallback marker";
    std::cout.rdbuf( previousConsoleBuffer );
    QVERIFY( QString::fromLocal8Bit( consoleOutput.str() ).contains( "console fallback marker" ) );

    logging::enableFileLogging( false );
    logging::enableFileLogging( false );
    QVERIFY( logging::currentLogFilePath().isEmpty() );
}

QTEST_GUILESS_MAIN( StorageConsumersTest )

#include "storageconsumerstest.moc"
