#include "legacystorage.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString normalized( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

} // namespace

std::optional<LegacyStorage> LegacyStorageDetector::detect( const QString& applicationDirectory,
                                                            const QString& userSettingsDirectory,
                                                            const QString& oldCrashDirectory )
{
    const QDir application{ normalized( applicationDirectory ) };
    const QString portableConfig
        = normalized( application.filePath( QStringLiteral( "ZzLogg.conf" ) ) );
    if ( QFileInfo{ portableConfig }.isFile() ) {
        return LegacyStorage{
            StorageMode::ProgramDirectory, portableConfig,
            normalized( application.filePath( QStringLiteral( "ZzLogg_session.conf" ) ) ),
            normalized( application.filePath( QStringLiteral( "klogg_dump" ) ) )
        };
    }

    const QDir userSettings{ normalized( userSettingsDirectory ) };
    const QString userConfig
        = normalized( userSettings.filePath( QStringLiteral( "ZzLogg.ini" ) ) );
    const QString userSession
        = normalized( userSettings.filePath( QStringLiteral( "ZzLogg_session.ini" ) ) );
    if ( QFileInfo{ userConfig }.isFile() || QFileInfo{ userSession }.isFile() ) {
        return LegacyStorage{ StorageMode::UserDirectory, userConfig, userSession,
                              normalized( oldCrashDirectory ) };
    }

    return std::nullopt;
}
