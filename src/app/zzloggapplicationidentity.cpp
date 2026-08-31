#include "zzloggapplicationidentity.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QUrl>

#include "zzlogg_brand.h"

void prepareZzLoggApplicationIdentity()
{
    QCoreApplication::setApplicationName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QGuiApplication::setApplicationDisplayName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QCoreApplication::setOrganizationName(
        QString::fromLatin1( zzlogg::brand::Vendor ) );
    const QUrl homepageUrl(
        QString::fromLatin1( zzlogg::brand::HomepageUrl ) );
    QCoreApplication::setOrganizationDomain( homepageUrl.host() );
}

bool applyZzLoggApplicationIcon( QApplication& app, QString* error )
{
    const QIcon icon( QString::fromLatin1( zzlogg::brand::IconResource ) );
    if ( icon.isNull() ) {
        if ( error ) {
            *error = QStringLiteral( "%1 application icon could not be loaded" )
                         .arg( QString::fromLatin1( zzlogg::brand::ProductName ) );
        }
        return false;
    }
    app.setWindowIcon( icon );
    return true;
}
