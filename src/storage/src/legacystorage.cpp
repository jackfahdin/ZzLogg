#include "legacystorage.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

QString normalized( const QString& path )
{
    if ( path.isEmpty() ) {
        return {};
    }
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

bool hasSettingsKeys( const QString& path )
{
    if ( !QFileInfo{ path }.isFile() ) {
        return false;
    }
    QSettings settings{ path, QSettings::IniFormat };
    return !settings.allKeys().isEmpty();
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
        const QString portableSession
            = normalized( application.filePath( QStringLiteral( "ZzLogg_session.conf" ) ) );
        return LegacyStorage{
            StorageMode::ProgramDirectory, portableConfig,
            QFileInfo{ portableSession }.isFile() ? portableSession : portableConfig,
            normalized( application.filePath( QStringLiteral( "klogg_dump" ) ) )
        };
    }

    const QDir userSettings{ normalized( userSettingsDirectory ) };
    const QString userConfig
        = normalized( userSettings.filePath( QStringLiteral( "ZzLogg.ini" ) ) );
    const QString userSession
        = normalized( userSettings.filePath( QStringLiteral( "ZzLogg_session.ini" ) ) );
    const QString userSessionConf
        = normalized( userSettings.filePath( QStringLiteral( "ZzLogg_session.conf" ) ) );
    const bool hasUserConfig = QFileInfo{ userConfig }.isFile();
    const bool hasUserSession = QFileInfo{ userSession }.isFile();
    const bool hasUserSessionConf = QFileInfo{ userSessionConf }.isFile();
    if ( hasUserConfig || hasUserSession || hasUserSessionConf ) {
        const QString sessionSource
            = hasSettingsKeys( userSession )
                  ? userSession
                  : hasUserSessionConf ? userSessionConf
                                       : hasUserConfig ? userConfig : userSession;
        return LegacyStorage{ StorageMode::UserDirectory, userConfig, sessionSource,
                              normalized( oldCrashDirectory ) };
    }

    return std::nullopt;
}
