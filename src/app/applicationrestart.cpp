#include "applicationrunner.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString resolveRestartExecutablePath( const QString& argv0 )
{
    const QString executable = QDir::fromNativeSeparators( argv0 );
    if ( executable.isEmpty() ) {
        return {};
    }

    const QFileInfo executableInfo{ executable };
    if ( executableInfo.isAbsolute() || executable.contains( QLatin1Char( '/' ) ) ) {
        return executableInfo.absoluteFilePath();
    }

    const QString found = QStandardPaths::findExecutable( executable );
    return found.isEmpty() ? QString{} : QFileInfo{ found }.absoluteFilePath();
}
