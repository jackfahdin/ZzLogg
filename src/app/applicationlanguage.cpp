#include "applicationlanguage.h"

QString preBootstrapLanguage( const QLocale& locale )
{
    if ( locale.language() != QLocale::Chinese ) {
        return QStringLiteral( "en" );
    }

    const QString languageTag = locale.bcp47Name();
    if ( languageTag.contains( QStringLiteral( "-Hant" ), Qt::CaseInsensitive )
         || locale.territory() == QLocale::Taiwan || locale.territory() == QLocale::HongKong
         || locale.territory() == QLocale::Macau ) {
        return QStringLiteral( "zh_TW" );
    }
    return QStringLiteral( "zh_CN" );
}
