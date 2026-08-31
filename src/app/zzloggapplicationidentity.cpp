#include "zzloggapplicationidentity.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>

#include "zzlogg_brand.h"

void prepareZzLoggApplicationIdentity()
{
    QCoreApplication::setApplicationName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QGuiApplication::setApplicationDisplayName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QCoreApplication::setOrganizationName(
        QString::fromLatin1( zzlogg::brand::Vendor ) );
    QCoreApplication::setOrganizationDomain( QStringLiteral( "gitcode.com" ) );
}

bool applyZzLoggApplicationIcon( QApplication& app, QString* error )
{
    const QIcon icon( QString::fromLatin1( zzlogg::brand::IconResource ) );
    if ( icon.isNull() ) {
        if ( error ) {
            *error = QStringLiteral( "ZzLogg application icon could not be loaded" );
        }
        return false;
    }
    app.setWindowIcon( icon );
    return true;
}
