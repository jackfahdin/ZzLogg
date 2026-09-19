#include "storagebootstrap.h"

#include "legacystorage.h"
#include "storagelocator.h"
#include "storagemigrator.h"
#include "storagevalidator.h"
#include "zzlogg_brand.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <utility>

namespace {

StorageBootstrapResult errorResult( QString error )
{
    return { StorageBootstrapStatus::Error, std::move( error ) };
}

bool samePath( const QString& left, const QString& right )
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath( QDir::fromNativeSeparators( left ) )
               .compare( QDir::cleanPath( QDir::fromNativeSeparators( right ) ), caseSensitivity )
           == 0;
}

bool sameLocation( const StorageLocation& left, const StorageLocation& right )
{
    return left.mode == right.mode && samePath( left.dataRoot, right.dataRoot )
           && samePath( left.locatorPath, right.locatorPath )
           && left.commandLineOverride == right.commandLineOverride;
}

StorageBootstrapResult installLocation( StorageLocation location,
                                        const StorageRuntimePaths& runtimePaths,
                                        bool requireManifest, bool writeManifest )
{
    if ( requireManifest && !QFileInfo{ location.dataRoot }.isDir() ) {
        return errorResult(
            QStringLiteral( "storage directory is missing: %1" ).arg( location.dataRoot ) );
    }
    const auto validation = StorageValidator::validate( location.dataRoot, true );
    if ( !validation.valid ) {
        return errorResult(
            validation.error.isEmpty()
                ? QStringLiteral( "invalid storage directory: %1" ).arg( location.dataRoot )
                : validation.error );
    }
    location.dataRoot = validation.normalizedRoot;
    if ( requireManifest && !StorageValidator::hasCompatibleManifest( location.dataRoot ) ) {
        return errorResult( QStringLiteral( "storage directory has no compatible manifest: %1" )
                                .arg( location.dataRoot ) );
    }

    const StorageContext context{ location };
    QString error;
    if ( !context.ensureDirectories( &error ) ) {
        return errorResult( error );
    }
    if ( writeManifest && !StorageValidator::writeManifest( context, &error ) ) {
        return errorResult( error );
    }
    if ( !StorageContext::install( context.location(), runtimePaths, &error ) ) {
        return errorResult( QStringLiteral( "failed to install storage context for %1: %2" )
                                .arg( context.dataRoot(), error ) );
    }
    return { StorageBootstrapStatus::Ready, {} };
}

StorageBootstrapResult installResolvedLocator( const StorageResolution& resolution,
                                               const StorageRuntimePaths& runtimePaths )
{
    if ( !resolution.error.isEmpty() ) {
        return errorResult( resolution.error );
    }
    if ( !resolution.state.has_value() ) {
        return errorResult(
            QStringLiteral( "storage locator did not contain an active location" ) );
    }
    return installLocation( resolution.state->active, runtimePaths, true, false );
}

StorageLocation legacySourceLocation( const LegacyStorage& legacy, const StorageLocatorStore& store,
                                      const QString& applicationDirectory,
                                      const QString& userSettingsDirectory )
{
    if ( legacy.mode == StorageMode::ProgramDirectory ) {
        return { StorageMode::ProgramDirectory, applicationDirectory, store.programLocatorPath(),
                 false };
    }
    return { StorageMode::UserDirectory, userSettingsDirectory, store.userLocatorPath(), false };
}

StorageLocation legacyTargetLocation( const LegacyStorage& legacy, const StorageLocatorStore& store,
                                      const QString& applicationDirectory,
                                      const QString& userDataDirectory )
{
    if ( legacy.mode == StorageMode::ProgramDirectory ) {
        return { StorageMode::ProgramDirectory,
                 QDir{ applicationDirectory }.filePath( QStringLiteral( "data" ) ),
                 store.programLocatorPath(), false };
    }
    return { StorageMode::UserDirectory, userDataDirectory, store.userLocatorPath(), false };
}

StorageBootstrapResult resolveExisting( const StorageLocatorStore& store,
                                        StorageResolution resolution,
                                        const StorageRuntimePaths& runtimePaths )
{
    for ( int recoveryAttempt = 0; recoveryAttempt < 3; ++recoveryAttempt ) {
        if ( !resolution.error.isEmpty() ) {
            return errorResult( resolution.error );
        }
        if ( !resolution.state.has_value() ) {
            return errorResult(
                QStringLiteral( "storage locator did not contain an active location" ) );
        }
        if ( !resolution.state->pending.has_value() ) {
            return installResolvedLocator( resolution, runtimePaths );
        }

        const StorageMigrationRequest request = *resolution.state->pending;
        const StorageMigrator migrator{ store };
        const StorageMigrationResult recovered = migrator.recoverPending( request );
        if ( recovered.success ) {
            resolution = store.resolve();
            continue;
        }
        if ( recovered.rolledBack ) {
            const StorageMigrationResult retried = migrator.execute( request );
            if ( !retried.success ) {
                return errorResult(
                    QStringLiteral( "failed to resume storage migration from %1 to %2: %3" )
                        .arg( request.source.dataRoot, request.target.dataRoot, retried.error ) );
            }
            resolution = store.resolve();
            continue;
        }
        return errorResult(
            QStringLiteral( "failed to recover pending storage migration from %1 to %2: %3" )
                .arg( request.source.dataRoot, request.target.dataRoot, recovered.error ) );
    }
    return errorResult( QStringLiteral( "storage migration recovery did not converge" ) );
}

} // namespace

StorageBootstrapResult
bootstrapStorage( const QString& applicationDirectory, const QString& appConfigDirectory,
                  const QString& userDataDirectory, const QString& legacyUserSettingsDirectory,
                  const QString& commandLineDataRoot,
                  StorageSelectionProvider selectionProvider )
{
    const StorageRuntimePaths runtimePaths{ applicationDirectory, appConfigDirectory,
                                            userDataDirectory };
    const StorageLocatorStore store{ applicationDirectory, appConfigDirectory };
    StorageResolution resolution = store.resolve( commandLineDataRoot );
    if ( resolution.source == StorageResolutionSource::CommandLine ) {
        if ( !resolution.error.isEmpty() ) {
            return errorResult(
                QStringLiteral( "%1: %2" ).arg( resolution.error, commandLineDataRoot ) );
        }
        if ( !resolution.state.has_value() ) {
            return errorResult(
                QStringLiteral( "command-line storage root could not be resolved: %1" )
                    .arg( commandLineDataRoot ) );
        }
        StorageLocation location = resolution.state->active;
        location.commandLineOverride = true;
        location.locatorPath.clear();
        return installLocation( std::move( location ), runtimePaths, false, true );
    }

    if ( ( resolution.source == StorageResolutionSource::ProgramLocator
           || resolution.source == StorageResolutionSource::UserLocator )
         && resolution.error.isEmpty() && resolution.state.has_value()
         && !resolution.state->pending.has_value() && !resolution.state->verified
         && !StorageValidator::hasCompatibleManifest( resolution.state->active.dataRoot ) ) {
        const auto legacy = LegacyStorageDetector::detect(
            applicationDirectory, legacyUserSettingsDirectory );
        if ( legacy.has_value()
             && sameLocation( resolution.state->active,
                              legacySourceLocation( *legacy, store, applicationDirectory,
                                                    legacyUserSettingsDirectory ) ) ) {
            QString error;
            if ( !store.discardUnverifiedLegacyLocator( resolution.state->active, &error ) ) {
                return errorResult( error );
            }
            resolution = store.resolve();
        }
    }

    if ( resolution.source == StorageResolutionSource::ProgramLocator
         || resolution.source == StorageResolutionSource::UserLocator ) {
        return resolveExisting( store, std::move( resolution ), runtimePaths );
    }

    if ( !selectionProvider ) {
        StorageLocation location{ StorageMode::UserDirectory, userDataDirectory, {}, false };
        return installLocation( std::move( location ), runtimePaths, false, true );
    }

    const auto legacy = LegacyStorageDetector::detect(
        applicationDirectory, legacyUserSettingsDirectory );
    if ( legacy.has_value() ) {
        const StorageLocation source = legacySourceLocation( *legacy, store, applicationDirectory,
                                                             legacyUserSettingsDirectory );
        StorageLocation target
            = legacyTargetLocation( *legacy, store, applicationDirectory, userDataDirectory );
        const auto validation = StorageValidator::validate( target.dataRoot, false );
        if ( !validation.valid ) {
            return errorResult(
                validation.error.isEmpty()
                    ? QStringLiteral( "invalid migration target: %1" ).arg( target.dataRoot )
                    : validation.error );
        }
        target.dataRoot = validation.normalizedRoot;
        StorageMigrationRequest request{};
        request.transactionId = QUuid::createUuid().toString( QUuid::WithoutBraces );
        request.source = source;
        request.target = target;
        request.legacyConfigFile = legacy->configFile;
        request.legacySessionFile = legacy->sessionFile;
        const StorageMigrationResult migrated = StorageMigrator{ store }.execute( request );
        if ( !migrated.success ) {
            return errorResult(
                QStringLiteral( "failed to migrate legacy storage from %1 to %2: %3" )
                    .arg( source.dataRoot, target.dataRoot, migrated.error ) );
        }
        return resolveExisting( store, store.resolve(), runtimePaths );
    }

    const std::optional<StorageLocation> selected
        = selectionProvider( { applicationDirectory, userDataDirectory } );
    if ( !selected.has_value() ) {
        return { StorageBootstrapStatus::Cancelled, {} };
    }

    StorageLocation location = *selected;
    location.commandLineOverride = false;
    const auto validation = StorageValidator::validate( location.dataRoot, true );
    if ( !validation.valid ) {
        return errorResult( validation.error.isEmpty()
                                ? QStringLiteral( "invalid selected storage directory: %1" )
                                      .arg( location.dataRoot )
                                : validation.error );
    }
    location.dataRoot = validation.normalizedRoot;
    const StorageContext context{ location };
    QString error;
    if ( !context.ensureDirectories( &error ) ) {
        return errorResult( error );
    }
    const bool manifestExisted = QFileInfo::exists( context.manifestFilePath() );
    if ( !StorageValidator::writeManifest( context, &error ) ) {
        return errorResult( error );
    }
    if ( !store.writeActive( context.location(), &error ) ) {
        if ( !manifestExisted && QFileInfo::exists( context.manifestFilePath() )
             && !QFile::remove( context.manifestFilePath() ) ) {
            error.append( QStringLiteral( "; failed to remove newly written storage manifest: %1" )
                              .arg( context.manifestFilePath() ) );
        }
        return errorResult( error );
    }
    return resolveExisting( store, store.resolve(), runtimePaths );
}
