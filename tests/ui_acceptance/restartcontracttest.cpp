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
    void cleanup()
    {
        app_.cancelApplicationExitPreparation();
        app_.setProperty( "zzlogg.test.failSessionSync", false );
        for ( auto* window : app_.mainWindows() ) {
            window->setProperty( "zzlogg.test.rejectApplicationClose", false );
            window->closeForApplicationExit();
        }
        app_.destroyMainWindows();
    }

    // A prepare implementation that closes windows instead of retaining them must fail here.
    void preparationRetainsRealWindows()
    {
        app_.setQuitOnLastWindowClosed( false );
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        first->show();
        second->show();
        QSignalSpy closed{ first, &MainWindow::windowClosed };
        QVERIFY( app_.prepareApplicationExit() );
        QVERIFY( first->isVisible() );
        QVERIFY( second->isVisible() );
        QCOMPARE( closed.count(), 0 );
        QVERIFY( !first->close() );
        QVERIFY( !first->closeForApplicationExit() );
        QVERIFY( !app_.prepareApplicationExit() );
        QVERIFY( app_.isApplicationExitPrepared() );
        app_.cancelApplicationExitPreparation();
        app_.cancelApplicationExitPreparation();
        QVERIFY( first->isEnabled() );
        QVERIFY( second->isEnabled() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( !app_.commitApplicationExit() );
    }

    // Losing either the app/window input guard would add a document or window.
    void preparedStateRejectsSnapshotMutationsAndRestoresEnabledState()
    {
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        first->show();
        second->show();
        second->setEnabled( false );
        auto* trayOpen = first->findChild<QAction*>( "trayOpenAction" );
        auto* trayQuit = first->findChild<QAction*>( "trayQuitAction" );
        QVERIFY( trayOpen && trayQuit );
        trayOpen->setEnabled( false );
        QTemporaryFile file;
        QVERIFY( file.open() );
        file.write( "new log\n" );
        file.flush();
        auto* tabs = first->findChild<DocumentWorkspace*>();
        QVERIFY( tabs );
        QCOMPARE( tabs->count(), 0 );
        QVERIFY( app_.prepareApplicationExit() );
        QVERIFY( !first->isEnabled() );
        QVERIFY( !trayQuit->isEnabled() );
        QVERIFY( app_.newWindow() == nullptr );
        Q_EMIT first->newWindow();
        app_.loadFileNonInteractive( file.fileName() );
        first->loadFileNonInteractive( file.fileName() );
        first->loadInitialFile( file.fileName(), false );
        Q_EMIT first->exitRequested();
        Q_EMIT first->restartRequested();
        trayQuit->trigger();
        auto* tray = first->findChild<QSystemTrayIcon*>();
        QVERIFY( tray );
        Q_EMIT tray->activated( QSystemTrayIcon::Trigger );
        QCOMPARE( app_.mainWindows().size(), 2 );
        QCOMPARE( tabs->count(), 0 );
        QVERIFY( first->isVisible() );
        QVERIFY( second->isVisible() );
        QVERIFY( app_.isApplicationExitPrepared() );
        app_.cancelApplicationExitPreparation();
        QVERIFY( first->isEnabled() );
        QVERIFY( !second->isEnabled() );
        QVERIFY( !trayOpen->isEnabled() );
        QVERIFY( trayQuit->isEnabled() );
        first->loadFileNonInteractive( file.fileName() );
        QCOMPARE( tabs->count(), 1 );
    }

    // A late rejection or sync failure must undo an earlier window's preparation.
    void failedPreparationRestoresWindowsAndSessionExitFlag_data()
    {
        QTest::addColumn<bool>( "syncFailure" );
        QTest::newRow( "window-rejects" ) << false;
        QTest::newRow( "session-sync-fails" ) << true;
    }
    void failedPreparationRestoresWindowsAndSessionExitFlag()
    {
        QFETCH( bool, syncFailure );
        MainWindow* first = app_.newWindow();
        const QString firstId = SessionInfo::getSynced().windows().constLast();
        MainWindow* second = app_.newWindow();
        first->show();
        second->show();
        QSignalSpy closed{ second, &MainWindow::windowClosed };
        first->setProperty( "zzlogg.test.rejectApplicationClose", !syncFailure );
        app_.setProperty( "zzlogg.test.failSessionSync", syncFailure );
        QVERIFY( !app_.prepareApplicationExit() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( first->isVisible() && second->isVisible() );
        QVERIFY( first->isEnabled() && second->isEnabled() );
        QCOMPARE( closed.count(), 0 );
        first->setProperty( "zzlogg.test.rejectApplicationClose", false );
        app_.setProperty( "zzlogg.test.failSessionSync", false );
        QVERIFY( app_.prepareApplicationExit() );
        app_.cancelApplicationExitPreparation();
        QVERIFY( first->closeForApplicationExit() );
        QVERIFY( !SessionInfo::getSynced().windows().contains( firstId ) );
    }

    // Preflight every participant before closing even the first window.
    void commitRejectsChangedWindowWithoutClosingAnyWindow()
    {
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        first->show();
        second->show();
        QSignalSpy closed{ second, &MainWindow::windowClosed };
        QVERIFY( app_.prepareApplicationExit() );
        first->setProperty( "zzlogg.test.rejectApplicationClose", true );
        QVERIFY( !app_.commitApplicationExit() );
        QCOMPARE( closed.count(), 0 );
        QVERIFY( first->isVisible() && second->isVisible() );
        QVERIFY( first->isEnabled() && second->isEnabled() );
        QVERIFY( !app_.isApplicationExitPrepared() );
    }

    void destroyedParticipantCancelsPreparation()
    {
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        second->show();
        QVERIFY( app_.prepareApplicationExit() );
        delete first;
        QVERIFY( !app_.commitApplicationExit() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( second->isEnabled() && second->isVisible() );
        QCOMPARE( app_.mainWindows().size(), 1 );
    }

    // Participant loss is announced so an in-flight update handoff cancels its
    // session instead of discovering the loss only at commit time; an explicit
    // cancel is not a loss and stays silent.
    void destroyedParticipantEmitsPreparationLost()
    {
        MainWindow* first = app_.newWindow();
        MainWindow* second = app_.newWindow();
        second->show();
        QSignalSpy lost{ &app_, &KloggApp::applicationExitPreparationLost };
        QVERIFY( lost.isValid() );
        QVERIFY( app_.prepareApplicationExit() );
        delete first;
        QCOMPARE( lost.count(), 1 );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( second->isEnabled() && second->isVisible() );
        QVERIFY( app_.prepareApplicationExit() );
        app_.cancelApplicationExitPreparation();
        QCOMPARE( lost.count(), 1 );
        QVERIFY( second->isEnabled() );
    }

    // A deferred deletion from an earlier normal close is not a lost participant.
    void retiredWindowDestructionDoesNotCancelCurrentPreparation()
    {
        MainWindow* retired = app_.newWindow();
        MainWindow* current = app_.newWindow();
        current->show();
        QVERIFY( retired->closeForApplicationExit() );
        QVERIFY( app_.prepareApplicationExit() );
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        QVERIFY( app_.isApplicationExitPrepared() );
        QVERIFY( !current->isEnabled() );
        app_.cancelApplicationExitPreparation();
        QVERIFY( current->isEnabled() );
    }

    // A queued QApplication quit must not bypass the still-reversible preparation.
    void preparedStateRejectsQueuedApplicationQuit()
    {
        MainWindow* window = app_.newWindow();
        window->show();
        QVERIFY( app_.prepareApplicationExit() );
        QTimer quitRequest;
        quitRequest.setSingleShot( true );
        connect( &quitRequest, &QTimer::timeout, &app_, &QCoreApplication::quit );
        QTimer finish;
        finish.setSingleShot( true );
        connect( &finish, &QTimer::timeout, &app_, [] { QCoreApplication::exit( 37 ); } );
        quitRequest.start( 0 );
        finish.start( 30 );
        QCOMPARE( app_.exec(), 37 );
        QVERIFY( window->isVisible() );
        QVERIFY( app_.isApplicationExitPrepared() );
    }

    void emptyPreparationIsSafeAndCommitNeedsPreparation()
    {
        QVERIFY( app_.mainWindows().isEmpty() );
        QVERIFY( !app_.commitApplicationExit() );
        QVERIFY( app_.prepareApplicationExit() );
        QVERIFY( !app_.prepareApplicationExit() );
        app_.cancelApplicationExitPreparation();
        QVERIFY( !app_.commitApplicationExit() );
        QVERIFY( app_.prepareApplicationExit() );
        QVERIFY( app_.commitApplicationExit() );
    }

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

    // Commit must keep the saved multiwindow snapshot and bypass minimize-to-tray.
    void commitPreservesThePreparedSessionSnapshot()
    {
        auto& config = Configuration::getSynced();
        config.setMinimizeToTray( true );
        config.save();
        MainWindow* first = app_.newWindow();
        const QString firstId = SessionInfo::getSynced().windows().constLast();
        MainWindow* second = app_.newWindow();
        const QString secondId = SessionInfo::getSynced().windows().constLast();
        first->resize( 817, 613 );
        second->resize( 921, 719 );
        first->show();
        second->show();
        QSignalSpy firstClosed{ first, &MainWindow::windowClosed };
        QSignalSpy secondClosed{ second, &MainWindow::windowClosed };
        QVERIFY( app_.prepareApplicationExit() );
        QFile snapshotFile( PersistentInfo::getSettings( session_settings{} ).fileName() );
        QVERIFY( snapshotFile.open( QIODevice::ReadOnly ) );
        const QByteArray snapshot = snapshotFile.readAll();
        snapshotFile.close();
        QVERIFY( !snapshot.isEmpty() );
        QVERIFY( app_.commitApplicationExit() );
        QCOMPARE( firstClosed.count(), 1 );
        QCOMPARE( secondClosed.count(), 1 );
        QVERIFY( !first->isVisible() && !second->isVisible() );
        QVERIFY( app_.mainWindows().isEmpty() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        auto& settings = PersistentInfo::getSettings( session_settings{} );
        settings.sync();
        QVERIFY( snapshotFile.open( QIODevice::ReadOnly ) );
        QCOMPARE( snapshotFile.readAll(), snapshot );
        QVERIFY( SessionInfo::getSynced().windows().contains( firstId ) );
        QVERIFY( SessionInfo::getSynced().windows().contains( secondId ) );
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
