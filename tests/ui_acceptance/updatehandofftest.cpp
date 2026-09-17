// Real-QApplication acceptance of the restricted update handoff controller.
// The success chain composes the production algorithms (exit preparation,
// directory activity reservation, native coordinator handshake) against the
// real handoff fixture child; error mapping uses scripted sessions only. No
// production entry point gains a success switch.

#include "applicationupdatehandoff.h"

#include "applicationupdateguard.h"
#include "configuration.h"
#include "coordinator_p.h"
#include "installationactivity_win.h"
#include "installlock_win.h"
#include "kloggapp.h"
#include "localchannel_win_p.h"
#include "mainwindow.h"
#include "storagecontext.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using zzlogg::updater::ActivityError;
using zzlogg::updater::DirectoryIdentity;
using zzlogg::updater::InstallLock;
using zzlogg::updater::InstallLockError;
using zzlogg::updater::InstallationActivity;
using zzlogg::updater::detail::Coordinator;
using zzlogg::updater::detail::CoordinationResult;
using zzlogg::updater::detail::after;

using Handoff = ApplicationUpdateHandoff;
using State = ApplicationUpdateHandoff::State;
using Failure = ApplicationUpdateHandoff::Failure;

namespace {

// Dedicated-test native backend over the real 3B.3 Coordinator. Every call
// runs on the controller worker thread with bounded internal deadlines.
class FixtureSession final : public Handoff::CoordinationSession {
  public:
    FixtureSession( QString fixture, QString base, std::optional<DirectoryIdentity> identity )
        : fixture_( std::move( fixture ) )
        , base_( std::move( base ) )
        , identity_( identity )
    {
    }
    Failure start() override
    {
        coordinator_ = std::make_unique<Coordinator>();
        if ( !coordinator_->start( fixture_.toStdWString(), base_.toStdWString(),
                                   identity_ ? &*identity_ : nullptr ) ) {
            return Failure::LaunchFailed;
        }
        if ( !coordinator_->authenticate( after( 3000 ) ) ) {
            return coordinator_->process().alive() ? Failure::PeerRejected : Failure::PeerLost;
        }
        if ( coordinator_->awaitAppExit( after( 3000 ) ) != CoordinationResult::WaitingForAppExit ) {
            return coordinator_->process().alive() ? Failure::PeerRejected : Failure::PeerLost;
        }
        return Failure::None;
    }
    bool canCommitExit() const override
    {
        return coordinator_ && coordinator_->canCommitExit();
    }
    bool commitExit() override
    {
        return coordinator_ && coordinator_->commitExit( after( 2000 ) );
    }
    void cancel() override
    {
        if ( coordinator_ ) {
            coordinator_->cancel( after( 1000 ) );
        }
    }

  private:
    QString fixture_;
    QString base_;
    std::optional<DirectoryIdentity> identity_;
    std::unique_ptr<Coordinator> coordinator_;
};

// Error-mapping backend: the handshake itself is exercised by FixtureSession.
class ScriptedSession final : public Handoff::CoordinationSession {
  public:
    explicit ScriptedSession( Failure startResult, bool canCommit = false )
        : startResult_( startResult )
        , canCommit_( canCommit )
    {
    }
    Failure start() override { return startResult_; }
    bool canCommitExit() const override { return canCommit_; }
    bool commitExit() override { return false; }
    void cancel() override {}

  private:
    Failure startResult_;
    bool canCommit_;
};

Handoff::SessionFactory fixtureSessionFactory( QString fixture, QString base )
{
    return [ fixture = QDir::toNativeSeparators( fixture ),
             base = QDir::toNativeSeparators( base ) ]( const Handoff::Request& request )
               -> std::unique_ptr<Handoff::CoordinationSession> {
        return std::make_unique<FixtureSession>( fixture, base, request.directory );
    };
}

Handoff::SessionFactory scriptedSessionFactory( Failure result, bool canCommit = false )
{
    return [ result, canCommit ]( const Handoff::Request& )
               -> std::unique_ptr<Handoff::CoordinationSession> {
        return std::make_unique<ScriptedSession>( result, canCommit );
    };
}

// Captures the request a composed factory actually received (3C task 5: the
// request carries the verified package path and selection context alongside
// the reserved directory identity).
struct CapturedRequest {
    std::optional<DirectoryIdentity> directory;
    QString packagePath;
    QString releaseVersion;
};

Handoff::SessionFactory capturingSessionFactory( QString fixture, QString base,
                                                 std::shared_ptr<CapturedRequest> capture )
{
    return [ fixture = QDir::toNativeSeparators( fixture ),
             base = QDir::toNativeSeparators( base ),
             capture = std::move( capture ) ]( const Handoff::Request& request )
               -> std::unique_ptr<Handoff::CoordinationSession> {
        capture->directory = request.directory;
        capture->packagePath = request.packagePath;
        capture->releaseVersion = request.releaseVersion;
        return std::make_unique<FixtureSession>( fixture, base, request.directory );
    };
}

QString fixtureExecutable()
{
    return QProcessEnvironment::systemEnvironment().value( "ZZLOGG_HANDOFF_FIXTURE_EXE" );
}

void setFixtureMode( const wchar_t* mode )
{
    SetEnvironmentVariableW( L"ZZLOGG_HANDOFF_FIXTURE", mode );
}

// begin() has real side effects, so it must not sit inside QTRY_VERIFY (that
// macro re-evaluates the expression, and a retry after a successful attempt is
// refused). A refused attempt rolls back cleanly; retrying rides out the
// conservative gate blocking while a previous fixture child still holds its
// bootstrap observer for a few moments after cancellation/failure.
bool beginWithRetry( ApplicationUpdateHandoff& handoff, qint64 timeoutMs = 10000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !handoff.begin() ) {
        if ( timer.elapsed() > timeoutMs ) {
            qWarning() << "beginWithRetry exhausted; last failure:"
                       << int( handoff.snapshot().failure ) << handoff.snapshot().detail;
            return false;
        }
        QTest::qWait( 50 );
    }
    return true;
}

// Independent child process: runs the real chain to a committed exit. Writes
// the committed marker only after exitCommitted and exits with code 0.
int runCommitChain( int argc, char* argv[], const QStringList& chainArgs )
{
    KloggApp app( argc, argv );
    app.setQuitOnLastWindowClosed( false );
    QTemporaryDir storage;
    QString error;
    if ( !StorageContext::install( { StorageMode::CustomDirectory, storage.path(),
                                     storage.filePath( QStringLiteral( "storage.ini" ) ), true },
                                   &error ) ) {
        return 3;
    }
    Configuration::getSynced();
    if ( app.updateGuard().enter( chainArgs[2] ) != ApplicationUpdateGuard::Status::Active ) {
        return 4;
    }
    auto* window = app.newWindow();
    window->show();
    ApplicationUpdateHandoff handoff( app, fixtureSessionFactory( chainArgs[0], chainArgs[1] ) );
    QObject::connect( &handoff, &ApplicationUpdateHandoff::snapshotChanged, &app,
                      [ &handoff ]( const ApplicationUpdateHandoff::Snapshot& snapshot ) {
                          if ( snapshot.state == State::Waiting && !handoff.commitExit() ) {
                              QCoreApplication::exit( 7 );
                          }
                          if ( snapshot.state == State::Failed || snapshot.state == State::Cancelled ) {
                              QCoreApplication::exit( 7 );
                          }
                      } );
    QObject::connect( &handoff, &ApplicationUpdateHandoff::exitCommitted, &app, [ & ] {
        QFile file( chainArgs[3] );
        if ( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            file.write( QStringLiteral( "committed windows=%1" )
                            .arg( app.mainWindows().size() )
                            .toUtf8() );
        }
        QCoreApplication::exit( 0 );
    } );
    QTimer::singleShot( 60000, &app, [] { QCoreApplication::exit( 8 ); } );
    if ( !handoff.begin() ) {
        return 5;
    }
    return app.exec();
}

} // namespace

class UpdateHandoffTest final : public QObject {
    Q_OBJECT

  public:
    explicit UpdateHandoffTest( KloggApp& app )
        : app_( app )
    {
    }

  private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY( root_.isValid() );
        installDir_ = root_.filePath( QStringLiteral( "Install" ) );
        QVERIFY( QDir().mkpath( installDir_ ) );
        QVERIFY( app_.updateGuard().enter( installDir_ )
                 == ApplicationUpdateGuard::Status::Active );
        QVERIFY( !fixtureExecutable().isEmpty() );
        QVERIFY( QFileInfo::exists( fixtureExecutable() ) );
    }

    void cleanup()
    {
        app_.cancelApplicationExitPreparation();
        app_.setProperty( "zzlogg.test.failSessionSync", false );
        setFixtureMode( nullptr );
        for ( auto* window : app_.mainWindows() ) {
            window->setProperty( "zzlogg.test.rejectApplicationClose", false );
            window->closeForApplicationExit();
        }
        app_.destroyMainWindows();
    }

    // The production seam has no execution capability: begin is refused and no
    // preparation, reservation or exit commit can happen.
    void closedProductionCapabilityRefusesBegin()
    {
        auto* window = app_.newWindow();
        window->show();
        ApplicationUpdateHandoff handoff( app_ );
        QVERIFY( !handoff.executionAvailable() );
        QVERIFY( !handoff.begin() );
        QCOMPARE( handoff.snapshot().state, State::Failed );
        QCOMPARE( handoff.snapshot().failure, Failure::ExecutionClosed );
        QVERIFY( !handoff.commitExit() );
        QVERIFY( window->isVisible() );
        QVERIFY( window->isEnabled() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( !app_.updateGuard().isUpdateReserved() );
    }

    // Brief contract: real prepare + reserve + native handshake to Waiting;
    // cancel restores the untouched window and releases the reservation.
    // QTRY on begin(): a previous fixture child may still hold its bootstrap
    // observer for a moment after cancellation/failure, and the conservative
    // blocking policy refuses a new reservation until the observed mutex
    // object is gone.
    void beginWaitsAndCancelRestoresWindow()
    {
        setFixtureMode( L"success" );
        auto* window = app_.newWindow();
        window->show();
        ApplicationUpdateHandoff handoff(
            app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
        QSignalSpy closed( window, &MainWindow::windowClosed );
        QVERIFY( beginWithRetry( handoff ) );
        QVERIFY( !handoff.begin() ); // repeated click while preparing is rejected
        QTRY_VERIFY( handoff.isWaiting() );
        QVERIFY( window->isVisible() );
        QVERIFY( !window->isEnabled() ); // prepared snapshot frozen
        QVERIFY( app_.isApplicationExitPrepared() );
        QVERIFY( app_.updateGuard().isUpdateReserved() );
        handoff.cancel();
        QTRY_VERIFY( window->isEnabled() );
        QCOMPARE( closed.count(), 0 );
        QCOMPARE( handoff.snapshot().state, State::Cancelled );
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( !handoff.commitExit() );
        // Cancellation is complete: a fresh begin works.
        QVERIFY( beginWithRetry( handoff ) );
        QTRY_VERIFY( handoff.isWaiting() );
        handoff.cancel();
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
        QCoreApplication::processEvents();
        QCOMPARE( closed.count(), 0 );
    }

    // Generation isolation: a completion that arrives after cancel must never
    // close windows or commit the exit.
    void lateSuccessAfterCancelCannotExit()
    {
        setFixtureMode( L"timeout" ); // child sleeps 500 ms before connecting
        auto* window = app_.newWindow();
        window->show();
        ApplicationUpdateHandoff handoff(
            app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
        QSignalSpy closed( window, &MainWindow::windowClosed );
        QVERIFY( beginWithRetry( handoff ) );
        handoff.cancel();
        QCOMPARE( handoff.snapshot().state, State::Cancelled );
        QTRY_VERIFY( window->isEnabled() );
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
        QTest::qWait( 2500 ); // let the late completion reach the event loop
        QCoreApplication::processEvents();
        QCOMPARE( handoff.snapshot().state, State::Cancelled );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( window->isVisible() );
        QVERIFY( window->isEnabled() );
        QCOMPARE( closed.count(), 0 );
        QVERIFY( !handoff.commitExit() );
    }

    // Every failure form keeps the original window usable.
    void failurePathsKeepWindowsUsable()
    {
        // Session save failure: begin is refused and preparation rolls back.
        {
            auto* window = app_.newWindow();
            window->show();
            app_.setProperty( "zzlogg.test.failSessionSync", true );
            ApplicationUpdateHandoff handoff(
                app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
            QVERIFY( !handoff.begin() );
            QCOMPARE( handoff.snapshot().state, State::Failed );
            QCOMPARE( handoff.snapshot().failure, Failure::PreparationFailed );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
            QVERIFY( !app_.isApplicationExitPrepared() );
            app_.setProperty( "zzlogg.test.failSessionSync", false );
        }
        // Another active instance in the same directory blocks the reservation.
        {
            InstallationActivity other;
            QVERIFY( other.enter( QDir::toNativeSeparators( installDir_ ).toStdWString() )
                     == ActivityError::None );
            auto* window = app_.newWindow();
            window->show();
            ApplicationUpdateHandoff handoff(
                app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
            QVERIFY( !handoff.begin() );
            QCOMPARE( handoff.snapshot().state, State::Failed );
            QCOMPARE( handoff.snapshot().failure, Failure::ReservationBlocked );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
            QVERIFY( !app_.updateGuard().isUpdateReserved() );
        }
        // Real launch failure: the coordinator image does not exist.
        {
            auto* window = app_.newWindow();
            window->show();
            ApplicationUpdateHandoff handoff(
                app_, fixtureSessionFactory( root_.filePath( QStringLiteral( "missing.exe" ) ),
                                             nextRuntimeBase() ) );
            QVERIFY( beginWithRetry( handoff ) );
            QTRY_COMPARE( handoff.snapshot().state, State::Failed );
            QCOMPARE( handoff.snapshot().failure, Failure::LaunchFailed );
            QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
            QVERIFY( !app_.isApplicationExitPrepared() );
        }
        // Handshake rejection / timeout / early peer exit.
        for ( const wchar_t* mode : { L"token", L"exit-after-hello", L"order" } ) {
            setFixtureMode( mode );
            auto* window = app_.newWindow();
            window->show();
            QSignalSpy closed( window, &MainWindow::windowClosed );
            ApplicationUpdateHandoff handoff(
                app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
            QVERIFY( beginWithRetry( handoff ) );
            QTRY_COMPARE( handoff.snapshot().state, State::Failed );
            QVERIFY( handoff.snapshot().failure == Failure::PeerRejected
                     || handoff.snapshot().failure == Failure::PeerLost );
            QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
            QVERIFY( !app_.isApplicationExitPrepared() );
            QCOMPARE( closed.count(), 0 );
        }
        setFixtureMode( nullptr );
        // UAC refusal is only an error mapping here; no real UAC is claimed.
        {
            auto* window = app_.newWindow();
            window->show();
            ApplicationUpdateHandoff handoff(
                app_, scriptedSessionFactory( Failure::ApprovalDeclined ) );
            QVERIFY( beginWithRetry( handoff ) );
            QTRY_COMPARE( handoff.snapshot().state, State::Failed );
            QCOMPARE( handoff.snapshot().failure, Failure::ApprovalDeclined );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
            QVERIFY( !app_.isApplicationExitPrepared() );
        }
        // A Ready-only peer (no real AwaitingAppExit) must never reach Waiting.
        {
            auto* window = app_.newWindow();
            window->show();
            ApplicationUpdateHandoff handoff(
                app_, scriptedSessionFactory( Failure::None, /*canCommit*/ false ) );
            QVERIFY( beginWithRetry( handoff ) );
            QTRY_COMPARE( handoff.snapshot().state, State::Failed );
            QCOMPARE( handoff.snapshot().failure, Failure::PeerLost );
            QVERIFY( !handoff.isWaiting() );
            QVERIFY( window->isVisible() );
            QVERIFY( window->isEnabled() );
        }
    }

    // A peer that dies after AwaitingAppExit cannot be committed; the window
    // stays usable.
    void peerLostBeforeCommitKeepsWindow()
    {
        setFixtureMode( L"die-waiting" ); // child exits after AwaitingAppExit
        auto* window = app_.newWindow();
        window->show();
        QSignalSpy closed( window, &MainWindow::windowClosed );
        ApplicationUpdateHandoff handoff(
            app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
        QVERIFY( beginWithRetry( handoff ) );
        QTRY_VERIFY( handoff.isWaiting() );
        QTest::qWait( 500 ); // let the peer exit
        QVERIFY( !handoff.commitExit() );
        QCOMPARE( handoff.snapshot().state, State::Failed );
        QCOMPARE( handoff.snapshot().failure, Failure::PeerLost );
        QTRY_VERIFY( window->isEnabled() );
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QCOMPARE( closed.count(), 0 );
    }

    // Only a live authenticated AwaitingAppExit peer authorizes the commit;
    // the commit closes the prepared windows and holds the reservation for the
    // real process exit (released here only as test-side cleanup).
    void commitAfterRealAwaitingAppExitClosesWindows()
    {
        setFixtureMode( L"success" );
        auto* first = app_.newWindow();
        first->show();
        ApplicationUpdateHandoff handoff(
            app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
        QVERIFY( !handoff.commitExit() ); // Idle must not commit
        QSignalSpy committed( &handoff, &ApplicationUpdateHandoff::exitCommitted );
        QSignalSpy closed( first, &MainWindow::windowClosed );
        QVERIFY( beginWithRetry( handoff ) );
        QTRY_VERIFY( handoff.isWaiting() );
        QVERIFY( first->isVisible() );
        QVERIFY( handoff.commitExit() );
        QVERIFY( !handoff.commitExit() ); // duplicate commit while committing
        QTRY_COMPARE( handoff.snapshot().state, State::ExitCommitted );
        QCOMPARE( committed.count(), 1 );
        QCOMPARE( closed.count(), 1 );
        QTRY_VERIFY( app_.mainWindows().isEmpty() );
        QVERIFY( !app_.isApplicationExitPrepared() );
        QVERIFY( app_.updateGuard().isUpdateReserved() );
        app_.updateGuard().cancelUpdate(); // test-side cleanup only
        QVERIFY( !app_.updateGuard().isUpdateReserved() );
    }

    // 3C task 5: the coordination request carries the reserved directory
    // identity plus the verified installer package path and release version
    // offered by the application. The offer comes from the app's own
    // download snapshot (empty in this uninstalled test environment); the
    // production capability stays closed regardless.
    void beginRequestCarriesPackageContext()
    {
        setFixtureMode( L"success" );
        auto* window = app_.newWindow();
        window->show();
        auto capture = std::make_shared<CapturedRequest>();
        ApplicationUpdateHandoff handoff(
            app_, capturingSessionFactory( fixtureExecutable(), nextRuntimeBase(), capture ) );
        QVERIFY( beginWithRetry( handoff ) );
        QTRY_VERIFY( handoff.isWaiting() );
        QVERIFY( capture->directory.has_value() );
        QVERIFY( app_.updateGuard().reservedIdentity().has_value() );
        QCOMPARE( capture->directory->volumeSerial,
                  app_.updateGuard().reservedIdentity()->volumeSerial );
        QCOMPARE( capture->directory->fileId, app_.updateGuard().reservedIdentity()->fileId );
        const auto offer = app_.verifiedUpdateOffer();
        QCOMPARE( capture->packagePath, offer.first );
        QCOMPARE( capture->releaseVersion, offer.second );
        handoff.cancel();
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
    }

    // The whole success chain in an independent process: the GUI really exits
    // with code 0 (never the restart code), and the child coordinator's
    // observer keeps the directory gate closed until the child itself ends.
    void commitChainRunsInIndependentProcessAndGateSurvivesParentExit()
    {
        QTemporaryDir chainRoot;
        QVERIFY( chainRoot.isValid() );
        const QString install = chainRoot.filePath( QStringLiteral( "Install" ) );
        const QString runtime = chainRoot.filePath( QStringLiteral( "Runtime" ) );
        QVERIFY( QDir().mkpath( install ) );
        QVERIFY( QDir().mkpath( runtime ) );
        const QString resultPath = chainRoot.filePath( QStringLiteral( "commit-result.txt" ) );
        const QString gateName
            = QStringLiteral( "Local\\zzlogg-handoff-gate-%1" )
                  .arg( QCoreApplication::applicationPid() );
        HANDLE gateRaw = CreateEventW( nullptr, TRUE, FALSE,
                                       reinterpret_cast<LPCWSTR>( gateName.utf16() ) );
        QVERIFY( gateRaw != nullptr );
        const auto gateCloser = qScopeGuard( [ gateRaw ] { CloseHandle( gateRaw ); } );
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert( QStringLiteral( "ZZLOGG_HANDOFF_FIXTURE" ),
                            QStringLiteral( "waitgate" ) );
        environment.insert( QStringLiteral( "ZZLOGG_HANDOFF_GATE" ), gateName );
        QProcess child;
        child.setProcessEnvironment( environment );
        child.setProgram( QCoreApplication::applicationFilePath() );
        child.setArguments( { QStringLiteral( "--commit-chain" ),
                              QDir::toNativeSeparators( fixtureExecutable() ),
                              QDir::toNativeSeparators( runtime ),
                              QDir::toNativeSeparators( install ), resultPath,
                              QStringLiteral( "-platform" ), QStringLiteral( "offscreen" ) } );
        child.start();
        QVERIFY( child.waitForFinished( 90000 ) );
        QCOMPARE( child.exitCode(), 0 );
        QFile result( resultPath );
        QVERIFY( result.open( QIODevice::ReadOnly ) );
        QCOMPARE( QString::fromUtf8( result.readAll() ).trimmed(),
                  QStringLiteral( "committed windows=0" ) );
        result.close();
        const auto nativeInstall = QDir::toNativeSeparators( install ).toStdWString();
        {
            InstallationActivity probe;
            QVERIFY( probe.enter( nativeInstall ) == ActivityError::Blocked );
        }
        {
            InstallLock lock;
            const auto claimResult = lock.acquire( nativeInstall );
            // The object lives on through the observing grandchild: blocked or
            // observed as abandoned, but never acquired.
            QVERIFY( claimResult == InstallLockError::Blocked
                     || claimResult == InstallLockError::Abandoned );
        }
        QVERIFY( SetEvent( gateRaw ) );
        QTRY_VERIFY_WITH_TIMEOUT(
            [ &nativeInstall ] {
                InstallationActivity probe;
                return probe.enter( nativeInstall ) == ActivityError::None;
            }(),
            15000 );
    }

    // Review finding (3B.4 task 3): losing a prepared participant silently
    // cancels the local preparation; the irreversible CommitExit must never be
    // posted afterwards and the application stays usable.
    void preparationLostBeforeCommitNeverSendsCommitExit()
    {
        setFixtureMode( L"waitgate" );
        const QString gateName
            = QStringLiteral( "Local\\zzlogg-handoff-gate-%1-lost" )
                  .arg( QCoreApplication::applicationPid() );
        HANDLE gateRaw = CreateEventW( nullptr, TRUE, FALSE,
                                       reinterpret_cast<LPCWSTR>( gateName.utf16() ) );
        QVERIFY( gateRaw != nullptr );
        const auto gateCloser = qScopeGuard( [ gateRaw ] { CloseHandle( gateRaw ); } );
        SetEnvironmentVariableW( L"ZZLOGG_HANDOFF_GATE",
                                 reinterpret_cast<const wchar_t*>( gateName.utf16() ) );
        const auto gateEnvCleaner
            = qScopeGuard( [] { SetEnvironmentVariableW( L"ZZLOGG_HANDOFF_GATE", nullptr ); } );
        auto* window = app_.newWindow();
        window->show();
        ApplicationUpdateHandoff handoff(
            app_, fixtureSessionFactory( fixtureExecutable(), nextRuntimeBase() ) );
        QVERIFY( beginWithRetry( handoff ) );
        QTRY_VERIFY( handoff.isWaiting() );
        QVERIFY( app_.isApplicationExitPrepared() );
        QVERIFY( app_.updateGuard().isUpdateReserved() );
        // Participant loss silently cancels the local preparation.
        delete window;
        QVERIFY( !app_.isApplicationExitPrepared() );
        // The commit path must refuse before posting the irreversible message.
        QVERIFY( !handoff.commitExit() );
        QCOMPARE( handoff.snapshot().state, State::Failed );
        QCOMPARE( handoff.snapshot().failure, Failure::PreparationFailed );
        QTRY_VERIFY( !app_.updateGuard().isUpdateReserved() );
        // The child received Cancel, not CommitExit: it exits after its bounded
        // receive instead of lingering on the never-signalled gate, so the
        // observed directory gate evaporates quickly.
        const auto nativeInstall = QDir::toNativeSeparators( installDir_ ).toStdWString();
        QTRY_VERIFY_WITH_TIMEOUT(
            [ &nativeInstall ] {
                InstallationActivity probe;
                return probe.enter( nativeInstall ) == ActivityError::None;
            }(),
            8000 );
        // The application stays fully usable.
        auto* replacement = app_.newWindow();
        QVERIFY( replacement != nullptr );
        replacement->show();
        QVERIFY( replacement->isEnabled() );
    }

  private:
    QString nextRuntimeBase()
    {
        const auto base = root_.filePath( QStringLiteral( "runtime-%1" ).arg( ++runtimeCounter_ ) );
        QDir().mkpath( base );
        return base;
    }

    KloggApp& app_;
    QTemporaryDir root_;
    QString installDir_;
    int runtimeCounter_ = 0;
};

int main( int argc, char* argv[] )
{
    QStandardPaths::setTestModeEnabled( true );
    QStringList arguments;
    for ( int index = 1; index < argc; ++index ) {
        arguments.append( QString::fromLocal8Bit( argv[index] ) );
    }
    const auto chainIndex = arguments.indexOf( QStringLiteral( "--commit-chain" ) );
    if ( chainIndex >= 0 && arguments.size() >= chainIndex + 5 ) {
        return runCommitChain( argc, argv, arguments.mid( chainIndex + 1, 4 ) );
    }
    QTemporaryDir root;
    if ( !root.isValid() ) {
        return 2;
    }
    KloggApp app( argc, argv );
    app.setQuitOnLastWindowClosed( false );
    QString error;
    if ( !StorageContext::install(
             { StorageMode::CustomDirectory, root.path(),
               root.filePath( QStringLiteral( "storage.ini" ) ), true },
             &error ) ) {
        qCritical().noquote() << error;
        return 3;
    }
    Configuration::getSynced();
    UpdateHandoffTest test{ app };
    return QTest::qExec( &test, argc, argv );
}

#include "updatehandofftest.moc"
