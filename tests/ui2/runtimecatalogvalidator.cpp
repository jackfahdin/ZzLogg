#include "runtimecatalogvalidator.h"

#include <QVector>
#include <QXmlStreamReader>

namespace {

struct CatalogMessage {
    int sourceElements = 0;
    QString source;
    int translationElements = 0;
    QString translation;
    QString translationType;
    bool numerus = false;
};

struct CatalogContext {
    int nameElements = 0;
    QString name;
    QVector<CatalogMessage> messages;
};

CatalogMessage readMessage( QXmlStreamReader& xml )
{
    CatalogMessage message;
    message.numerus = xml.attributes().hasAttribute( QStringLiteral( "numerus" ) );
    while ( xml.readNextStartElement() ) {
        if ( xml.name() == QStringLiteral( "source" ) ) {
            ++message.sourceElements;
            message.source = xml.readElementText( QXmlStreamReader::IncludeChildElements );
        }
        else if ( xml.name() == QStringLiteral( "translation" ) ) {
            ++message.translationElements;
            message.translationType
                = xml.attributes().value( QStringLiteral( "type" ) ).toString();
            message.translation
                = xml.readElementText( QXmlStreamReader::IncludeChildElements );
        }
        else {
            xml.skipCurrentElement();
        }
    }
    return message;
}

CatalogContext readContext( QXmlStreamReader& xml )
{
    CatalogContext context;
    while ( xml.readNextStartElement() ) {
        if ( xml.name() == QStringLiteral( "name" ) ) {
            ++context.nameElements;
            context.name = xml.readElementText( QXmlStreamReader::ErrorOnUnexpectedElement );
        }
        else if ( xml.name() == QStringLiteral( "message" ) ) {
            context.messages.push_back( readMessage( xml ) );
        }
        else {
            xml.skipCurrentElement();
        }
    }
    return context;
}

RuntimeCatalogValidationResult invalid( QString error )
{
    return { false, std::move( error ), {} };
}

} // namespace

RuntimeCatalogValidationResult validateRuntimeCatalog( const QByteArray& contents,
                                                       const QStringList& requiredSources )
{
    QXmlStreamReader xml{ contents };
    QVector<CatalogContext> contexts;

    if ( !xml.readNextStartElement() || xml.name() != QStringLiteral( "TS" ) ) {
        return invalid( QStringLiteral( "catalog root must be TS" ) );
    }
    while ( xml.readNextStartElement() ) {
        if ( xml.name() == QStringLiteral( "context" ) ) {
            contexts.push_back( readContext( xml ) );
        }
        else {
            xml.skipCurrentElement();
        }
    }
    if ( xml.hasError() ) {
        return invalid( QStringLiteral( "invalid TS XML: %1" ).arg( xml.errorString() ) );
    }

    QVector<const CatalogContext*> crawlerContexts;
    for ( const auto& context : contexts ) {
        if ( context.nameElements == 1 && context.name == QStringLiteral( "CrawlerWidget" ) ) {
            crawlerContexts.push_back( &context );
        }
    }
    if ( crawlerContexts.size() != 1 ) {
        return invalid( QStringLiteral( "expected exactly one CrawlerWidget context; found %1" )
                            .arg( crawlerContexts.size() ) );
    }

    RuntimeCatalogValidationResult result{ true, {}, {} };
    const CatalogContext& crawler = *crawlerContexts.constFirst();
    for ( const QString& requiredSource : requiredSources ) {
        QVector<const CatalogMessage*> matchingMessages;
        for ( const auto& message : crawler.messages ) {
            if ( message.source == requiredSource ) {
                matchingMessages.push_back( &message );
            }
        }
        if ( matchingMessages.size() != 1 ) {
            return invalid(
                QStringLiteral( "expected exactly one active CrawlerWidget message for '%1'; "
                                "found %2" )
                    .arg( requiredSource )
                    .arg( matchingMessages.size() ) );
        }

        const CatalogMessage& message = *matchingMessages.constFirst();
        if ( message.sourceElements != 1 ) {
            return invalid( QStringLiteral( "wrong source identity for '%1'" )
                                .arg( requiredSource ) );
        }
        if ( message.numerus ) {
            return invalid( QStringLiteral( "numerus message is not allowed for '%1'" )
                                .arg( requiredSource ) );
        }
        if ( message.translationElements != 1 ) {
            return invalid( QStringLiteral( "expected one translation element for '%1'; found %2" )
                                .arg( requiredSource )
                                .arg( message.translationElements ) );
        }
        if ( !message.translationType.isEmpty() ) {
            return invalid( QStringLiteral( "translation for '%1' has inactive type '%2'" )
                                .arg( requiredSource, message.translationType ) );
        }
        if ( message.translation.trimmed().isEmpty() ) {
            return invalid( QStringLiteral( "translation for '%1' is empty" )
                                .arg( requiredSource ) );
        }
        result.translations.insert( requiredSource, message.translation );
    }
    return result;
}
