#include "storagebootstrap.h"

#include "legacystorage.h"
#include "storagelocator.h"
#include "storagemigrator.h"
#include "storagevalidator.h"
#include "zzlogg_brand.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>

#include <utility>

namespace {

StorageBootstrapResult errorResult( QString error )
{
    return { StorageBootstrapStatus::Error, std::move( error ) };
}

StorageBootstrapResult installLocation( StorageLocation location, bool requireManifest,
                                        bool writeManifest )
{
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
    if ( !StorageContext::install( context.location(), &error ) ) {
        return errorResult( QStringLiteral( "failed to install storage context for %1: %2" )
                                .arg( context.dataRoot(), error ) );
    }
    return { StorageBootstrapStatus::Ready, {} };
}

StorageBootstrapResult installResolvedLocator( const StorageResolution& resolution )
{
    if ( !resolution.error.isEmpty() ) {
        return errorResult( resolution.error );
    }
    if ( !resolution.state.has_value() ) {
        return errorResult(
            QStringLiteral( "storage locator did not contain an active location" ) );
    }
    return installLocation( resolution.state->active, true, false );
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

QString legacyUserSettingsDirectory()
{
    const QSettings legacySettings{ QSettings::IniFormat, QSettings::UserScope,
                                    QString::fromLatin1( zzlogg::brand::SettingsOrganization ),
                                    QString::fromLatin1( zzlogg::brand::SettingsApplication ) };
    return QFileInfo{ legacySettings.fileName() }.absolutePath();
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
                                        StorageResolution resolution )
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
            return installResolvedLocator( resolution );
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
                  const QString& userDataDirectory, const QString& oldCrashDirectory,
                  const QString& commandLineDataRoot, StorageSelectionProvider selectionProvider )
{
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
        return installLocation( std::move( location ), false, true );
    }

    if ( resolution.source == StorageResolutionSource::ProgramLocator
         || resolution.source == StorageResolutionSource::UserLocator ) {
        return resolveExisting( store, std::move( resolution ) );
    }

    if ( !selectionProvider ) {
        StorageLocation location{ StorageMode::UserDirectory, userDataDirectory, {}, false };
        return installLocation( std::move( location ), false, true );
    }

    const QString userSettingsDirectory = legacyUserSettingsDirectory();
    const auto legacy = LegacyStorageDetector::detect( applicationDirectory, userSettingsDirectory,
                                                       oldCrashDirectory );
    if ( legacy.has_value() ) {
        const StorageLocation source
            = legacySourceLocation( *legacy, store, applicationDirectory, userSettingsDirectory );
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
        const StorageMigrationRequest request{ QUuid::createUuid().toString( QUuid::WithoutBraces ),
                                               source,
                                               target,
                                               legacy->configFile,
                                               legacy->sessionFile,
                                               legacy->crashDirectory };
        const StorageMigrationResult migrated = StorageMigrator{ store }.execute( request );
        if ( !migrated.success ) {
            return errorResult(
                QStringLiteral( "failed to migrate legacy storage from %1 to %2: %3" )
                    .arg( source.dataRoot, target.dataRoot, migrated.error ) );
        }
        return resolveExisting( store, store.resolve() );
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
    return resolveExisting( store, store.resolve() );
}
