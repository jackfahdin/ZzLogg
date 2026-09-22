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

#include "applicationupdateguard.h"

#include <QDir>
#include <QFileInfo>

#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
#include "installationactivity_win.h"
#endif

namespace {

#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
QString nativeDirectoryPath( const QString& directory )
{
    auto native = QDir::toNativeSeparators( directory );
    while ( native.size() > 3 && native.endsWith( QLatin1Char( '\\' ) ) ) {
        native.chop( 1 );
    }
    return native;
}
#endif

} // namespace

struct ApplicationUpdateGuard::Impl {
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    std::unique_ptr<zzlogg::updater::InstallationActivity> activity;
#endif
    Status status = Status::Inactive;
    ReservationError reservationError = ReservationError::None;
    QString errorText;
};

ApplicationUpdateGuard::ApplicationUpdateGuard()
    : impl_( std::make_unique<Impl>() )
{
}

ApplicationUpdateGuard::~ApplicationUpdateGuard() = default;
ApplicationUpdateGuard::ApplicationUpdateGuard( ApplicationUpdateGuard&& ) noexcept = default;
ApplicationUpdateGuard&
ApplicationUpdateGuard::operator=( ApplicationUpdateGuard&& ) noexcept = default;

ApplicationUpdateGuard::Status ApplicationUpdateGuard::enter( const QString& applicationDir )
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    if ( impl_->activity || impl_->status != Status::Inactive ) {
        return impl_->status;
    }
    auto activity = std::make_unique<zzlogg::updater::InstallationActivity>();
    const auto result = activity->enter( nativeDirectoryPath( applicationDir ).toStdWString() );
    switch ( result ) {
    case zzlogg::updater::ActivityError::None:
        impl_->activity = std::move( activity );
        impl_->status = Status::Active;
        break;
    case zzlogg::updater::ActivityError::InvalidRoot:
        // Unsupported path form (portable/network layout): manual use
        // continues with the update capability off.
        impl_->status = Status::Inactive;
        break;
    case zzlogg::updater::ActivityError::Blocked:
        impl_->status = Status::Blocked;
        impl_->errorText = QStringLiteral(
            "an update reservation or a preexisting lock object was observed for %1" )
                               .arg( applicationDir );
        break;
    case zzlogg::updater::ActivityError::Abandoned:
    case zzlogg::updater::ActivityError::Unavailable:
        impl_->status = Status::Unavailable;
        impl_->errorText = QStringLiteral(
            "the installation directory identity could not be verified for %1" )
                               .arg( applicationDir );
        break;
    }
    return impl_->status;
#else
    Q_UNUSED( applicationDir );
    return Status::Inactive;
#endif
}

ApplicationUpdateGuard::Status
ApplicationUpdateGuard::probeStartupWithoutLease( const QString& applicationDir )
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    switch ( zzlogg::updater::InstallationActivity::probeEntry(
                 nativeDirectoryPath( applicationDir ).toStdWString() ) ) {
    case zzlogg::updater::ActivityError::None:
    case zzlogg::updater::ActivityError::InvalidRoot:
        return Status::Inactive;
    case zzlogg::updater::ActivityError::Blocked:
        return Status::Blocked;
    default:
        return Status::Unavailable;
    }
#else
    Q_UNUSED( applicationDir );
    return Status::Inactive;
#endif
}

ApplicationUpdateGuard::Status ApplicationUpdateGuard::status() const
{
    return impl_->status;
}

bool ApplicationUpdateGuard::reserveUpdate()
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    if ( !impl_->activity || impl_->status != Status::Active ) {
        impl_->reservationError = ReservationError::Unavailable;
        impl_->errorText = QStringLiteral( "no active installation activity lease" );
        return false;
    }
    const auto result = impl_->activity->reserveUpdate();
    if ( result == zzlogg::updater::ActivityError::None ) {
        impl_->reservationError = ReservationError::None;
        impl_->errorText.clear();
        return true;
    }
    switch ( result ) {
    case zzlogg::updater::ActivityError::Blocked:
        impl_->reservationError = ReservationError::Blocked;
        impl_->errorText
            = QStringLiteral( "another instance is active in the installation directory" );
        break;
    case zzlogg::updater::ActivityError::Abandoned:
        impl_->reservationError = ReservationError::Abandoned;
        impl_->errorText = QStringLiteral(
            "a previous update reservation was abandoned by a holder that exited without "
            "releasing it" );
        break;
    default:
        impl_->reservationError = ReservationError::Unavailable;
        impl_->errorText
            = QStringLiteral( "the installation directory could not be exclusively verified" );
        break;
    }
    return false;
#else
    impl_->reservationError = ReservationError::Unavailable;
    impl_->errorText = QStringLiteral( "automatic update is not supported on this platform" );
    return false;
#endif
}

ApplicationUpdateGuard::ReservationError ApplicationUpdateGuard::reservationError() const
{
    return impl_->reservationError;
}

void ApplicationUpdateGuard::cancelUpdate()
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    if ( impl_->activity ) {
        impl_->activity->cancelUpdate();
    }
#endif
}

bool ApplicationUpdateGuard::isUpdateReserved() const
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    return impl_->activity && impl_->activity->updateReserved();
#else
    return false;
#endif
}

std::optional<zzlogg::updater::DirectoryIdentity> ApplicationUpdateGuard::reservedIdentity() const
{
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    if ( isUpdateReserved() ) {
        return impl_->activity->identity();
    }
#endif
    return std::nullopt;
}

QString ApplicationUpdateGuard::errorText() const
{
    return impl_->errorText;
}

QString ApplicationUpdateGuard::singleInstanceName( const QString& applicationDir,
                                                    const QString& applicationFilePath )
{
    const auto legacyName = QFileInfo( applicationFilePath ).fileName();
#ifdef ZZLOGG_HAS_INSTALLATION_ACTIVITY
    zzlogg::updater::DirectoryIdentity identity{};
    if ( zzlogg::updater::InstallationActivity::probeIdentity(
             nativeDirectoryPath( applicationDir ).toStdWString(), identity ) ) {
        QString scopedName = legacyName + QLatin1Char( '-' )
                             + QString::number( identity.volumeSerial, 16 ).rightJustified(
                                 16, QLatin1Char( '0' ) );
        for ( const auto byte : identity.fileId ) {
            scopedName += QString::number( byte, 16 ).rightJustified( 2, QLatin1Char( '0' ) );
        }
        return scopedName;
    }
#else
    Q_UNUSED( applicationDir );
#endif
    return legacyName;
}
