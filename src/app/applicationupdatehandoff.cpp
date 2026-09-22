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

#include "applicationupdatehandoff.h"

#include <atomic>

#include <QMetaObject>
#include <QThread>

#include "kloggapp.h"

struct ApplicationUpdateHandoff::Impl {
    Impl( KloggApp& ownApp, SessionFactory ownFactory )
        : app( ownApp )
        , factory( std::move( ownFactory ) )
    {
    }

    // Lazy worker thread: production keeps the closed capability and never
    // pays for it. The worker context only sequences bounded session calls;
    // every completion is posted back to the GUI thread with its generation.
    void ensureWorker()
    {
        if ( workerContext ) {
            return;
        }
        workerContext = new QObject;
        workerContext->moveToThread( &thread );
        thread.start();
    }

    void setSnapshot( ApplicationUpdateHandoff* owner, Snapshot next )
    {
        snapshot = next;
        Q_EMIT owner->snapshotChanged( snapshot );
    }

    KloggApp& app;
    SessionFactory factory;
    QThread thread;
    QObject* workerContext = nullptr;
    std::shared_ptr<CoordinationSession> session;
    std::atomic<quint64> generation{ 0 };
    Snapshot snapshot;
    unsigned long destructWaitMs = kDefaultDestructWaitMs;
    bool commitPending = false;
    bool reservationHeld = false;
    bool committed = false;
};

ApplicationUpdateHandoff::ApplicationUpdateHandoff( KloggApp& app, QObject* parent )
    : ApplicationUpdateHandoff( app, SessionFactory{}, parent )
{
}

ApplicationUpdateHandoff::ApplicationUpdateHandoff( KloggApp& app, SessionFactory factory,
                                                    QObject* parent )
    : QObject( parent )
    , impl_( std::make_unique<Impl>( app, std::move( factory ) ) )
{
    qRegisterMetaType<Snapshot>( "ApplicationUpdateHandoff::Snapshot" );
    // A destroyed prepared participant silently cancels the application
    // preparation; the notification cancels the in-flight session here so the
    // loss is handled at once instead of only at commit time.
    connect( &app, &KloggApp::applicationExitPreparationLost, this,
             &ApplicationUpdateHandoff::onPreparationLost );
}

ApplicationUpdateHandoff::~ApplicationUpdateHandoff()
{
    auto& d = *impl_;
    ++d.generation;
    if ( !d.committed && d.app.isApplicationExitPrepared() ) {
        d.app.cancelApplicationExitPreparation();
    }
    if ( d.workerContext ) {
        // Worker steps are bounded by the session deadlines; after the wait no
        // worker code can run, so dropping undelivered functors (and their
        // session references) here cannot race the native coordinator. The
        // coordinator's own reaper still covers a live child to its real end.
        d.thread.quit();
        if ( !d.thread.wait( d.destructWaitMs ) ) {
            // A hung native step must never block application teardown: release
            // the GUI-thread reservation, then deliberately leak the worker
            // (thread, context and session references) instead of waiting
            // forever or racing the still-running coordinator. Plain qWarning
            // on purpose: this diagnostic must survive disabled file logging.
            qWarning() << "ApplicationUpdateHandoff worker did not finish within"
                       << d.destructWaitMs << "ms during destruction; leaking the worker";
            if ( !d.committed && d.reservationHeld ) {
                d.app.updateGuard().cancelUpdate();
                d.reservationHeld = false;
            }
            impl_.release();
            return;
        }
        delete d.workerContext;
        d.workerContext = nullptr;
    }
    if ( !d.committed && d.reservationHeld ) {
        d.app.updateGuard().cancelUpdate();
        d.reservationHeld = false;
    }
}

bool ApplicationUpdateHandoff::executionAvailable() const
{
    // The production backend carries no execution capability this phase; only
    // dedicated test targets compose a session factory.
    return impl_->factory != nullptr;
}

void ApplicationUpdateHandoff::setDestructWaitBudgetForTesting( unsigned long budgetMs )
{
    impl_->destructWaitMs = budgetMs;
}

ApplicationUpdateHandoff::Snapshot ApplicationUpdateHandoff::snapshot() const
{
    return impl_->snapshot;
}

bool ApplicationUpdateHandoff::isWaiting() const
{
    return impl_->snapshot.state == State::Waiting;
}

bool ApplicationUpdateHandoff::begin()
{
    auto& d = *impl_;
    if ( d.snapshot.state == State::Preparing || d.snapshot.state == State::Waiting
         || d.committed ) {
        return false;
    }
    if ( d.reservationHeld ) {
        // The previous cancellation is still releasing the reservation on the
        // worker thread; refuse with a diagnosable snapshot, not silence.
        d.setSnapshot( this,
                       { d.snapshot.state, Failure::CancellationInProgress,
                         QStringLiteral(
                             "the previous cancellation is still releasing the installation "
                             "reservation" ) } );
        return false;
    }
    const auto fail = [ this, &d ]( Failure failure, const QString& detail ) {
        d.setSnapshot( this, { State::Failed, failure, detail } );
        return false;
    };
    if ( !d.factory ) {
        return fail( Failure::ExecutionClosed,
                     QStringLiteral( "update execution capability is closed" ) );
    }
    if ( !d.app.prepareApplicationExit() ) {
        return fail( Failure::PreparationFailed,
                     QStringLiteral( "session preparation failed" ) );
    }
    if ( !d.app.updateGuard().reserveUpdate() ) {
        const auto reservationError = d.app.updateGuard().reservationError();
        const auto failure = reservationError == ApplicationUpdateGuard::ReservationError::Blocked
            ? Failure::ReservationBlocked
            : reservationError == ApplicationUpdateGuard::ReservationError::Abandoned
                ? Failure::ReservationAbandoned
                : Failure::ReservationUnavailable;
        const auto detail = d.app.updateGuard().errorText();
        d.app.cancelApplicationExitPreparation();
        return fail( failure, detail );
    }
    d.reservationHeld = true;
    Request request{};
    request.directory = d.app.updateGuard().reservedIdentity();
    // The verified package offer is request data only: the production factory
    // stays absent, so it can never become execution authority this phase.
    const auto offer = d.app.verifiedUpdateOffer();
    request.packagePath = offer.packagePath;
    request.releaseVersion = offer.releaseVersion;
    request.installRoot = offer.installRoot;
    request.packageSize = offer.packageSize;
    request.packageSha256 = offer.packageSha256;
    auto session = d.factory( request );
    if ( !session ) {
        d.app.updateGuard().cancelUpdate();
        d.reservationHeld = false;
        d.app.cancelApplicationExitPreparation();
        return fail( Failure::ExecutionClosed,
                     QStringLiteral( "no coordination session available" ) );
    }
    d.session = std::shared_ptr<CoordinationSession>( std::move( session ) );
    d.ensureWorker();
    d.setSnapshot( this, { State::Preparing, Failure::None, {} } );
    const auto generation = ++d.generation;
    std::shared_ptr<CoordinationSession> workerSession = d.session;
    QMetaObject::invokeMethod(
        d.workerContext,
        [ this, generation, workerSession ]() mutable {
            const Failure failure = workerSession->start();
            const bool canCommit = failure == Failure::None && workerSession->canCommitExit();
            QMetaObject::invokeMethod(
                this,
                [ this, generation, workerSession, failure, canCommit ]() mutable {
                    onStartFinished( generation, std::move( workerSession ), failure, canCommit );
                },
                Qt::QueuedConnection );
        },
        Qt::QueuedConnection );
    return true;
}

void ApplicationUpdateHandoff::cancel()
{
    auto& d = *impl_;
    if ( d.snapshot.state != State::Preparing && d.snapshot.state != State::Waiting ) {
        return;
    }
    const auto generation = ++d.generation; // stale every pending completion
    d.commitPending = false;
    if ( d.app.isApplicationExitPrepared() ) {
        d.app.cancelApplicationExitPreparation();
    }
    if ( d.session ) {
        std::shared_ptr<CoordinationSession> workerSession = std::move( d.session );
        d.session.reset();
        // Serialized behind any in-flight call on the worker thread; the
        // reservation is released only after the peer has been cancelled.
        QMetaObject::invokeMethod(
            d.workerContext,
            [ this, generation, workerSession ]() mutable {
                workerSession->cancel();
                QMetaObject::invokeMethod(
                    this,
                    [ this, generation ]() {
                        if ( generation == impl_->generation && impl_->reservationHeld ) {
                            impl_->app.updateGuard().cancelUpdate();
                            impl_->reservationHeld = false;
                        }
                    },
                    Qt::QueuedConnection );
            },
            Qt::QueuedConnection );
    }
    else if ( d.reservationHeld ) {
        d.app.updateGuard().cancelUpdate();
        d.reservationHeld = false;
    }
    d.setSnapshot( this, { State::Cancelled, Failure::None, {} } );
}

bool ApplicationUpdateHandoff::commitExit()
{
    auto& d = *impl_;
    if ( d.snapshot.state != State::Waiting || d.commitPending || !d.session ) {
        return false;
    }
    // Recheck the local preparation before posting the irreversible CommitExit:
    // losing a prepared participant silently cancels the application
    // preparation, and the peer must never observe a committed exit for an
    // application that stays alive.
    if ( !d.app.isApplicationExitPrepared() ) {
        failFromWaiting( Failure::PreparationFailed,
                         QStringLiteral( "the prepared session was lost before the exit commit" ) );
        return false;
    }
    if ( !d.session->canCommitExit() ) {
        failFromWaiting( Failure::PeerLost,
                         QStringLiteral( "peer exited before the exit commit" ) );
        return false;
    }
    d.commitPending = true;
    const auto generation = d.generation.load();
    std::shared_ptr<CoordinationSession> workerSession = d.session;
    QMetaObject::invokeMethod(
        d.workerContext,
        [ this, generation, workerSession ]() mutable {
            const bool committed = workerSession->commitExit();
            QMetaObject::invokeMethod(
                this,
                [ this, generation, workerSession, committed ]() mutable {
                    onCommitFinished( generation, std::move( workerSession ), committed );
                },
                Qt::QueuedConnection );
        },
        Qt::QueuedConnection );
    return true;
}

void ApplicationUpdateHandoff::onStartFinished(
    quint64 generation, std::shared_ptr<CoordinationSession> session, Failure failure,
    bool canCommit )
{
    auto& d = *impl_;
    if ( generation != d.generation || d.snapshot.state != State::Preparing ) {
        // Stale after cancel/destroy: the caller-owned session reference dies
        // here; the native coordinator reaper still covers a live child.
        return;
    }
    if ( failure == Failure::None && canCommit ) {
        d.setSnapshot( this, { State::Waiting, Failure::None, {} } );
        return;
    }
    discardSessionOnWorker( std::move( session ) );
    d.session.reset();
    releaseReservation();
    if ( d.app.isApplicationExitPrepared() ) {
        d.app.cancelApplicationExitPreparation();
    }
    d.setSnapshot( this,
                   { State::Failed, failure == Failure::None ? Failure::PeerLost : failure, {} } );
}

void ApplicationUpdateHandoff::onCommitFinished(
    quint64 generation, std::shared_ptr<CoordinationSession> /*session*/, bool committed )
{
    auto& d = *impl_;
    d.commitPending = false;
    if ( generation != d.generation || d.snapshot.state != State::Waiting ) {
        // Cancelled while the commit was in flight: never commit the exit.
        return;
    }
    if ( !committed ) {
        failFromWaiting( Failure::CommitRejected,
                         QStringLiteral( "the peer rejected the exit commit" ) );
        return;
    }
    if ( !d.app.commitApplicationExit() ) {
        failFromWaiting( Failure::CommitRejected,
                         QStringLiteral( "the application exit commit failed" ) );
        return;
    }
    // The reservation intentionally stays held: the child's observer binds it
    // across this process's real exit, which the owner arranges on
    // exitCommitted.
    d.committed = true;
    d.setSnapshot( this, { State::ExitCommitted, Failure::None, {} } );
    Q_EMIT exitCommitted();
}

void ApplicationUpdateHandoff::onPreparationLost()
{
    auto& d = *impl_;
    if ( d.snapshot.state != State::Preparing && d.snapshot.state != State::Waiting ) {
        return;
    }
    // The preparation is already gone; stale every pending completion (start
    // or commit) so a late "successful" CommitExit can never commit the exit
    // of an application that just stayed alive.
    ++d.generation;
    d.commitPending = false;
    failFromWaiting( Failure::PreparationFailed,
                     QStringLiteral( "the prepared session was lost during the handoff" ) );
}

void ApplicationUpdateHandoff::failFromWaiting( Failure failure, const QString& detail )
{
    auto& d = *impl_;
    if ( d.session ) {
        discardSessionOnWorker( std::move( d.session ) );
        d.session.reset();
    }
    releaseReservation();
    if ( d.app.isApplicationExitPrepared() ) {
        d.app.cancelApplicationExitPreparation();
    }
    d.setSnapshot( this, { State::Failed, failure, detail } );
}

void ApplicationUpdateHandoff::releaseReservation()
{
    auto& d = *impl_;
    if ( d.reservationHeld ) {
        d.app.updateGuard().cancelUpdate();
        d.reservationHeld = false;
    }
}

void ApplicationUpdateHandoff::discardSessionOnWorker(
    std::shared_ptr<CoordinationSession> session )
{
    // Queued behind any in-flight worker call, so the cancel never races the
    // same native coordinator; the last reference then dies on the worker.
    QMetaObject::invokeMethod(
        impl_->workerContext, [ session ]() mutable { session->cancel(); }, Qt::QueuedConnection );
}
