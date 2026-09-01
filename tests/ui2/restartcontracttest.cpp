#include "applicationrunner.h"
#include "configuration.h"
#include "kloggapp.h"
#include "logger.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "persistentinfo.h"
#include "sessioninfo.h"
#include "storagecontext.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class RestartContractTest final : public QObject {
    Q_OBJECT

public:
    explicit RestartContractTest( KloggApp& app )
        : app_( app )
    {
    }

private Q_SLOTS:
    void optionsRestartIsForwardedAfterTheDialogStackUnwinds()
    {
        auto session = std::make_shared<Session>();
        MainWindow window{ WindowSession{ session, QStringLiteral( "queued-options" ), 0 } };
        QSignalSpy restartSpy{ &window, &MainWindow::restartRequested };
        bool wasQueued = false;
        QTimer::singleShot( 0, &window, [ & ] {
            OptionsDialog* dialog = nullptr;
            for ( QWidget* widget : QApplication::topLevelWidgets() ) {
                if ( auto* candidate = qobject_cast<OptionsDialog*>( widget ) ) {
                    dialog = candidate;
                    break;
                }
            }
            if ( dialog == nullptr ) {
                return;
            }
            Q_EMIT dialog->restartRequested();
            wasQueued = restartSpy.count() == 0;
            dialog->reject();
        } );

        QVERIFY( QMetaObject::invokeMethod( &window, "options", Qt::DirectConnection ) );
        QVERIFY( wasQueued );
        QCOMPARE( restartSpy.count(), 0 );
        QCoreApplication::processEvents();
        QCOMPARE( restartSpy.count(), 1 );
        QVERIFY( window.closeForApplicationExit() );
    }

    void prepareRejectionKeepsEveryWindowOpenAndRegistered()
    {
        app_.setQuitOnLastWindowClosed( false );
        app_.setProperty( "zzlogg.test.suppressRestartErrors", true );
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        first->show();
        second->show();
        QSignalSpy firstClosed{ first, &MainWindow::windowClosed };
        QSignalSpy secondClosed{ second, &MainWindow::windowClosed };
        first->setProperty( "zzlogg.test.rejectApplicationClose", true );

        Q_EMIT second->restartRequested();

        const QList<MainWindow*> registered = app_.mainWindows();
        const bool bothVisible = first->isVisible() && second->isVisible();
        const bool bothRegistered = registered.contains( first ) && registered.contains( second );
        const int firstCloseCount = firstClosed.count();
        const int secondCloseCount = secondClosed.count();
        first->setProperty( "zzlogg.test.rejectApplicationClose", false );
        const QList<MainWindow*> cleanupWindows = app_.mainWindows();
        for ( MainWindow* window : cleanupWindows ) {
            QVERIFY( window->closeForApplicationExit() );
        }

        QVERIFY( bothVisible );
        QVERIFY( bothRegistered );
        QCOMPARE( firstCloseCount, 0 );
        QCOMPARE( secondCloseCount, 0 );
    }

    void restartClosesWindowsSyncsSessionAndReturnsContractCode()
    {
        auto& config = Configuration::getSynced();
        config.setMinimizeToTray( true );
        config.save();
        MainWindow* window = app_.newWindow();
        const QString restartWindowId = SessionInfo::getSynced().windows().constLast();
        QVERIFY( !restartWindowId.isEmpty() );
        window->resize( 913, 617 );
        window->show();
        QSignalSpy closedSpy{ window, &MainWindow::windowClosed };
        logging::enableFileLogging( true, logging::LogLevel::Info );
        QVERIFY( !logging::currentLogFilePath().isEmpty() );

        app_.setProperty( "zzlogg.test.suppressRestartErrors", true );
        app_.setProperty( "zzlogg.test.failSessionSync", true );
        Q_EMIT window->restartRequested();
        QVERIFY( window->isVisible() );
        QCOMPARE( closedSpy.count(), 0 );
        app_.setProperty( "zzlogg.test.failSessionSync", false );
        window->setProperty( "zzlogg.test.rejectApplicationClose", true );
        Q_EMIT window->restartRequested();
        QVERIFY( window->isVisible() );
        QCOMPARE( closedSpy.count(), 0 );
        window->setProperty( "zzlogg.test.rejectApplicationClose", false );
        window->close();
        QVERIFY( !window->isVisible() );
        QCOMPARE( closedSpy.count(), 0 );
        window->show();

        QTimer::singleShot( 0, window, [ window ] { Q_EMIT window->restartRequested(); } );
        const int result = app_.exec();

        QCOMPARE( result, ZzLoggRestartExitCode );
        QCOMPARE( closedSpy.count(), 1 );
        QVERIFY( !window->isVisible() );
        QVERIFY( logging::currentLogFilePath().isEmpty() );
        auto& session = PersistentInfo::getSettings( session_settings{} );
        session.sync();
        QCOMPARE( session.status(), QSettings::NoError );
        QVERIFY( QFileInfo::exists( session.fileName() ) );
        session.beginGroup( QStringLiteral( "Window" ) );
        QCOMPARE( session.value( QStringLiteral( "version" ) ).toInt(), 1 );
        const int count = session.beginReadArray( QStringLiteral( "windows" ) );
        QVERIFY( count >= 1 );
        bool foundRestartWindow = false;
        for ( int index = 0; index < count; ++index ) {
            session.setArrayIndex( index );
            if ( session.value( QStringLiteral( "id" ) ).toString() == restartWindowId ) {
                foundRestartWindow
                    = !session.value( QStringLiteral( "geometry" ) ).toByteArray().isEmpty();
                break;
            }
        }
        QVERIFY( foundRestartWindow );
        session.endArray();
        session.endGroup();
    }

private:
    KloggApp& app_;
};

int main( int argc, char* argv[] )
{
    QStandardPaths::setTestModeEnabled( true );
    QTemporaryDir root;
    if ( !root.isValid() ) {
        return 2;
    }
    KloggApp app{ argc, argv };
    QString error;
    if ( !StorageContext::install(
             { StorageMode::CustomDirectory, root.path(),
               root.filePath( QStringLiteral( "storage.ini" ) ), true },
             &error ) ) {
        qCritical().noquote() << error;
        return 3;
    }
    Configuration::getSynced();
    RestartContractTest test{ app };
    return QTest::qExec( &test, argc, argv );
}

#include "restartcontracttest.moc"
