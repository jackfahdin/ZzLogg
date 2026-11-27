#include "runtimecatalogvalidator.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QtTest>

namespace {

QStringList runtimeSources()
{
    return { QStringLiteral( "Match case" ),
             QStringLiteral( "Use regex" ),
             QStringLiteral( "Inverse match" ),
             QStringLiteral( "Combine filter conditions" ),
             QStringLiteral( "Auto-refresh" ),
             QStringLiteral( "Edit search history" ),
             QStringLiteral(
                 "Keep these results and show subsequent results in a new window" ),
             QStringLiteral( "%1 matches found" ), QStringLiteral( "%1 match found" ),
             QStringLiteral( "File truncated on disk" ) };
}

} // namespace

class RuntimeCatalogContractTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void decodesEntitiesAndMultilineTranslations();
    void rejectsInvalidFixtures_data();
    void rejectsInvalidFixtures();
    void acceptsCurrentCatalogs_data();
    void acceptsCurrentCatalogs();
    void allSourceMessagesAreTranslated();
};

void RuntimeCatalogContractTest::decodesEntitiesAndMultilineTranslations()
{
    const QByteArray xml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<TS><context><name>CrawlerWidget</name><message>
<source>Use &lt;regex&gt; &amp; match</source>
<translation>first &amp;
second</translation>
</message></context></TS>)xml";

    const auto result = validateRuntimeCatalog(
        xml, { QStringLiteral( "Use <regex> & match" ) } );
    QVERIFY2( result.valid, qPrintable( result.error ) );
    QCOMPARE( result.translations.value( QStringLiteral( "Use <regex> & match" ) ),
              QStringLiteral( "first &\nsecond" ) );
}

void RuntimeCatalogContractTest::rejectsInvalidFixtures_data()
{
    QTest::addColumn<QByteArray>( "xml" );
    QTest::addColumn<QString>( "expectedError" );

    QTest::newRow( "missing-translation" )
        << QByteArray( "<TS><context><name>CrawlerWidget</name><message>"
                       "<source>Match case</source></message></context></TS>" )
        << QStringLiteral( "translation" );
    for ( const QByteArray type : { QByteArray( "unfinished" ), QByteArray( "vanished" ),
                                    QByteArray( "obsolete" ) } ) {
        QTest::newRow( type.constData() )
            << QByteArray( "<TS><context><name>CrawlerWidget</name><message>"
                           "<source>Match case</source><translation type=\"" )
                   + type
                   + QByteArray( "\">translated</translation></message></context></TS>" )
            << QString::fromLatin1( type );
    }
    QTest::newRow( "encoded-whitespace" )
        << QByteArray( "<TS><context><name>CrawlerWidget</name><message>"
                       "<source>Match case</source><translation>&#x20;&#x9;&#10;</translation>"
                       "</message></context></TS>" )
        << QStringLiteral( "empty" );
    QTest::newRow( "duplicate-message-identity" )
        << QByteArray( "<TS><context><name>CrawlerWidget</name>"
                       "<message><source>Match case</source><translation>one</translation></message>"
                       "<message><source>Match case</source><translation>two</translation></message>"
                       "</context></TS>" )
        << QStringLiteral( "exactly one" );
    QTest::newRow( "duplicate-context-identity" )
        << QByteArray( "<TS><context><name>CrawlerWidget</name>"
                       "<message><source>Match case</source><translation>one</translation></message>"
                       "</context><context><name>CrawlerWidget</name></context></TS>" )
        << QStringLiteral( "context" );
    QTest::newRow( "wrong-context-identity" )
        << QByteArray( "<TS><context><name>OtherWidget</name><message>"
                       "<source>Match case</source><translation>translated</translation>"
                       "</message></context></TS>" )
        << QStringLiteral( "CrawlerWidget" );
    QTest::newRow( "numerus-message" )
        << QByteArray( "<TS><context><name>CrawlerWidget</name>"
                       "<message numerus=\"yes\"><source>Match case</source>"
                       "<translation><numerusform>translated</numerusform></translation>"
                       "</message></context></TS>" )
        << QStringLiteral( "numerus" );
}

void RuntimeCatalogContractTest::rejectsInvalidFixtures()
{
    QFETCH( QByteArray, xml );
    QFETCH( QString, expectedError );

    const auto result = validateRuntimeCatalog( xml, { QStringLiteral( "Match case" ) } );
    QVERIFY( !result.valid );
    QVERIFY2( result.error.contains( expectedError, Qt::CaseInsensitive ),
              qPrintable( result.error ) );
}

void RuntimeCatalogContractTest::acceptsCurrentCatalogs_data()
{
    QTest::addColumn<QString>( "language" );
    QTest::newRow( "english" ) << QStringLiteral( "en" );
    QTest::newRow( "simplified-chinese" ) << QStringLiteral( "zh_CN" );
    QTest::newRow( "traditional-chinese" ) << QStringLiteral( "zh_TW" );
}

void RuntimeCatalogContractTest::acceptsCurrentCatalogs()
{
    QFETCH( QString, language );
    const QString path = QStringLiteral( ZZLOGG_SOURCE_ROOT "/src/app/i18n/%1.ts" )
                             .arg( language );
    QFile catalog{ path };
    QVERIFY2( catalog.open( QIODevice::ReadOnly ), qPrintable( catalog.errorString() ) );

    const auto result = validateRuntimeCatalog( catalog.readAll(), runtimeSources() );
    QVERIFY2( result.valid, qPrintable( QStringLiteral( "%1: %2" ).arg( language, result.error ) ) );
}

void RuntimeCatalogContractTest::allSourceMessagesAreTranslated()
{
    QTemporaryDir temporary;
    QVERIFY( temporary.isValid() );
    const auto extractedPath = temporary.filePath( "extracted.ts" );
    QProcess lupdate;
    lupdate.start( QStringLiteral( ZZLOGG_LUPDATE_EXECUTABLE ),
        { QStringLiteral( ZZLOGG_SOURCE_ROOT "/src" ), "-recursive", "-no-obsolete",
          "-ts", extractedPath } );
    QVERIFY( lupdate.waitForFinished( 30000 ) );
    QCOMPARE( lupdate.exitStatus(), QProcess::NormalExit );
    QCOMPARE( lupdate.exitCode(), 0 );

    struct Message { QString source, translation, type; };
    using Catalog = QMap<QString, Message>;
    QString parseError;
    const auto readCatalog = [&]( const QString& path ) {
        Catalog result;
        QFile file{ path };
        if ( !file.open( QIODevice::ReadOnly ) ) {
            parseError = file.errorString();
            return result;
        }
        QXmlStreamReader xml{ &file };
        QString context;
        while ( !xml.atEnd() ) {
            xml.readNext();
            if ( !xml.isStartElement() ) continue;
            if ( xml.name() == QLatin1String( "name" ) ) context = xml.readElementText();
            if ( xml.name() != QLatin1String( "message" ) ) continue;
            Message message;
            QString comment;
            while ( xml.readNextStartElement() ) {
                const auto tag = xml.name().toString();
                if ( tag == "translation" ) message.type = xml.attributes().value( "type" ).toString();
                const auto text = xml.readElementText( QXmlStreamReader::IncludeChildElements );
                if ( tag == "source" ) message.source = text;
                if ( tag == "translation" ) message.translation = text;
                if ( tag == "comment" ) comment = text;
            }
            if ( message.type == "obsolete" || message.type == "vanished" ) continue;
            const auto key = context + QChar( 0x1f ) + message.source + QChar( 0x1f ) + comment;
            if ( result.contains( key ) ) parseError = "Duplicate message: " + key;
            result.insert( key, message );
        }
        if ( xml.hasError() ) parseError = xml.errorString();
        return result;
    };
    const auto sources = readCatalog( extractedPath );
    QVERIFY2( parseError.isEmpty(), qPrintable( parseError ) );
    QVERIFY( !sources.isEmpty() );
    const auto placeholders = []( const QString& text ) {
        QStringList result;
        auto matches = QRegularExpression{ "%L?(?:[1-9][0-9]*|n)" }.globalMatch( text );
        while ( matches.hasNext() ) result.append( matches.next().captured() );
        result.sort();
        return result;
    };
    for ( const auto* language : { "en", "zh_CN", "zh_TW" } ) {
        const auto catalog = readCatalog( QStringLiteral( ZZLOGG_SOURCE_ROOT "/src/app/i18n/%1.ts" ).arg( language ) );
        QVERIFY2( parseError.isEmpty(), qPrintable( parseError ) );
        for ( auto it = sources.cbegin(); it != sources.cend(); ++it ) {
            QVERIFY2( catalog.contains( it.key() ), qPrintable( QString{ language } + ": missing " + it.key() ) );
        }
        for ( auto it = catalog.cbegin(); it != catalog.cend(); ++it ) {
            const auto description = QString{ language } + ": " + it.key();
            QVERIFY2( it->type.isEmpty(), qPrintable( description + " is unfinished" ) );
            QVERIFY2( !it->translation.trimmed().isEmpty(), qPrintable( description + " is empty" ) );
            QVERIFY2( placeholders( it->source ) == placeholders( it->translation ),
                      qPrintable( description + " has mismatched placeholders" ) );
        }
    }
}

QTEST_APPLESS_MAIN( RuntimeCatalogContractTest )

#include "runtimecatalogcontracttest.moc"
