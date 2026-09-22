/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KLOGG_APPLICATIONUPDATEHANDOFF_H
#define KLOGG_APPLICATIONUPDATEHANDOFF_H

#include <functional>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>

#include "installlock_win.h"

class KloggApp;

// Qt-side lifecycle controller for the restricted update handoff (3B.4).
// States: Idle -> Preparing -> Waiting -> ExitCommitted, with Cancelled and
// Failed reachable from Preparing/Waiting. The UI consumes snapshot() only;
// execution availability comes from the production backend and stays closed
// this phase, never derived from a Verified download state. All public
// methods are GUI-thread only; the GUI thread never blocks on channel IO or
// process waits.
class ApplicationUpdateHandoff : public QObject
{
    Q_OBJECT

  public:
    enum class State { Idle, Preparing, Waiting, ExitCommitted, Cancelled, Failed };
    Q_ENUM( State )
    enum class Failure {
        None,
        ExecutionClosed,        // production execution capability is closed
        PreparationFailed,      // session save/preparation failed
        ReservationBlocked,     // another instance is active in the directory
        ReservationAbandoned,   // a previous reservation holder died without releasing
        ReservationUnavailable, // directory identity could not be verified
        LaunchFailed,           // helper process could not be started
        PeerRejected,           // handshake rejected or timed out
        PeerLost,               // peer exited early
        CommitRejected,         // exit commit failed after AwaitingAppExit
        ApprovalDeclined,       // elevation declined (mapping only; no real UAC this phase)
        // Diagnostic only: a repeated begin while the previous cancellation
        // still holds the reservation. The snapshot state stays Cancelled.
        CancellationInProgress,
    };
    Q_ENUM( Failure )
    struct Snapshot {
        State state = State::Idle;
        Failure failure = Failure::None;
        // English diagnostic detail, never displayed directly.
        QString detail;
    };

    // Native coordination session, driven only on the controller worker thread
    // with bounded internal waits. Only dedicated test targets compose the real
    // 3B.3 Coordinator through this seam; production never returns a session.
    class CoordinationSession
    {
      public:
        virtual ~CoordinationSession() = default;
        // Bounded: launch the runtime copy, authenticate, await AwaitingAppExit.
        // Returns None only when the peer is authenticated, alive and waiting.
        virtual Failure start() = 0;
        virtual bool canCommitExit() const = 0;
        // Bounded: send CommitExit to the live authenticated peer.
        virtual bool commitExit() = 0;
        // Bounded best-effort Cancel, then disconnect.
        virtual void cancel() = 0;
    };
    struct Request {
        // Reserved installation directory identity; present only while the
        // application holds the update reservation.
        std::optional<zzlogg::updater::DirectoryIdentity> directory;
        // Verified installer package path (3B.2 output) and the selected
        // release version it was verified for. Data only, never execution
        // authority; the production capability stays closed this phase and
        // only dedicated test fixtures consume these fields.
        QString packagePath;
        QString releaseVersion;
        QString installRoot;
        quint64 packageSize = 0;
        QString packageSha256;
    };
    using SessionFactory
        = std::function<std::unique_ptr<CoordinationSession>( const Request& )>;

    // Destructor worker-wait budget in milliseconds: worker steps are bounded
    // by the session deadlines (seconds), so quitting and waiting this budget
    // is enough; it comfortably outlasts the launch + authenticate + await
    // chain of a well-behaved coordinator.
    static constexpr unsigned long kDefaultDestructWaitMs = 15000;

    // Production constructor: closed capability, begin() always refuses.
    explicit ApplicationUpdateHandoff( KloggApp& app, QObject* parent = nullptr );
    // Dedicated-target constructor used by tests to compose the real native
    // coordinator. Production code never passes a factory.
    ApplicationUpdateHandoff( KloggApp& app, SessionFactory factory, QObject* parent = nullptr );
    ~ApplicationUpdateHandoff() override;

    // Test seam: overrides the destructor's bounded worker wait (default
    // kDefaultDestructWaitMs). Production never calls this.
    void setDestructWaitBudgetForTesting( unsigned long budgetMs );

    bool executionAvailable() const;
    Snapshot snapshot() const;
    bool isWaiting() const;

    // Starts prepare -> reserve -> native coordination. Returns false unless
    // the asynchronous chain actually started; on immediate refusal the
    // snapshot already carries the Failed state.
    bool begin();
    // Cancels an active handoff: restores the prepared windows immediately and
    // releases the reservation once the peer has been cancelled. Late
    // completions can never commit the exit afterwards.
    void cancel();
    // Commits the application exit; only valid in Waiting with a live
    // authenticated AwaitingAppExit peer.
    bool commitExit();

  Q_SIGNALS:
    void snapshotChanged( const ApplicationUpdateHandoff::Snapshot& snapshot );
    // Emitted after the native CommitExit and the application commit. The
    // owner arranges the real process exit (never the restart exit code).
    void exitCommitted();

  private:
    void onStartFinished( quint64 generation, std::shared_ptr<CoordinationSession> session,
                          Failure failure, bool canCommit );
    void onCommitFinished( quint64 generation, std::shared_ptr<CoordinationSession> session,
                           bool committed );
    void onPreparationLost();
    void failFromWaiting( Failure failure, const QString& detail );
    void releaseReservation();
    void discardSessionOnWorker( std::shared_ptr<CoordinationSession> session );

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Q_DECLARE_METATYPE( ApplicationUpdateHandoff::Snapshot )

#endif // KLOGG_APPLICATIONUPDATEHANDOFF_H
