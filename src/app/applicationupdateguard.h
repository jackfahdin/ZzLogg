/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef KLOGG_APPLICATIONUPDATEGUARD_H
#define KLOGG_APPLICATIONUPDATEGUARD_H

#include <memory>
#include <optional>

#include <QString>

#include "installlock_win.h"

// Application-side anchor of the installation activity lease. Every method is
// UI-thread only: the underlying directory mutex is thread-affine and is never
// owned by, or moved to, the native coordinator IO worker. The object only
// coordinates; it carries no elevation, authentication or installation
// authority, and a successful reservation never proves that every file of the
// installation is replaceable.
class ApplicationUpdateGuard {
  public:
    enum class Status {
        // No lease held: unsupported path form (e.g. network/portable) or a
        // platform without automatic update capability. Manual use continues.
        Inactive,
        // Read-only activity lease held; update coordination available.
        Active,
        // An update reservation (or any preexisting lock object) was observed
        // for this installation directory. Startup must not proceed.
        Blocked,
        // The installation directory identity could not be established or
        // verified (permission failure, reparse anomaly, replacement). Startup
        // must not proceed; this is never treated as "unsupported".
        Unavailable,
    };

    ApplicationUpdateGuard();
    ~ApplicationUpdateGuard();
    ApplicationUpdateGuard( ApplicationUpdateGuard&& ) noexcept;
    ApplicationUpdateGuard& operator=( ApplicationUpdateGuard&& ) noexcept;
    ApplicationUpdateGuard( const ApplicationUpdateGuard& ) = delete;
    ApplicationUpdateGuard& operator=( const ApplicationUpdateGuard& ) = delete;

    // Enters the activity lease for the given application directory. Call once,
    // before any single-instance forwarding or storage bootstrap. The lease is
    // held until this object is destroyed (real process teardown).
    Status enter( const QString& applicationDir );
    Status status() const;

    // Update reservation for the update handoff controller. Failures are reported
    // read-only through errorText(); the activity lease is unaffected.
    bool reserveUpdate();
    void cancelUpdate();
    bool isUpdateReserved() const;
    // Precise kind of the last reserveUpdate() failure. Abandoned means the
    // reservation object outlived a holder that died without releasing it, as
    // opposed to Blocked (a live instance or lock object) and Unavailable
    // (identity could not be verified, or no active lease).
    enum class ReservationError { None, Blocked, Abandoned, Unavailable };
    ReservationError reservationError() const;
    // Immutable directory identity of the reserved installation; present only
    // while a reservation is held. Consumers derive the observer mutex name
    // from it; there is deliberately no setter.
    std::optional<zzlogg::updater::DirectoryIdentity> reservedIdentity() const;

    // Technical detail of the last failure (English diagnostic). UI maps Status
    // to translated text first; the detail may be appended after the translated
    // message for diagnosis, as the startup guard failure report does.
    QString errorText() const;

    // Directory-scoped single-instance name. Two directories of the same
    // installation identity (any path casing) share a name; different
    // installation directories are independent. Falls back to the legacy
    // executable-name behaviour when directory identity is unavailable
    // (unsupported path forms, non-Windows platforms).
    static QString singleInstanceName( const QString& applicationDir,
                                       const QString& applicationFilePath );

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // KLOGG_APPLICATIONUPDATEGUARD_H
