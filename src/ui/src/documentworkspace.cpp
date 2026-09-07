#include "documentworkspace.h"

#include "crawlerwidget.h"
#include "quickfindmux.h"
#include "session.h"
#include "signalmux.h"

DocumentWorkspace::DocumentWorkspace( WindowSession& session, SignalMux& mux,
                                      QuickFindMux& quickFind )
    : session_( session )
    , mux_( mux )
    , quickFind_( quickFind )
{
    setObjectName( QStringLiteral( "documentTabs" ) );
    setDocumentMode( true );
    setMovable( true );
    setTabsClosable( true );
    connect( this, &QTabWidget::currentChanged, this, &DocumentWorkspace::activateCurrentDocument );
}

DocumentWorkspace::~DocumentWorkspace()
{
    // Stop routing before Qt destroys the document children.
    mux_.setCurrentDocument( nullptr );
    quickFind_.registerSelector( nullptr );
    // The session can outlive this workspace. Remove its view registrations
    // without delivering current-document notifications to a tearing-down host.
    disconnect( this, nullptr, nullptr, nullptr );
    while ( count() > 0 )
        closeDocument( 0 );
}

CrawlerWidget* DocumentWorkspace::currentDocument() const
{
    return qobject_cast<CrawlerWidget*>( currentWidget() );
}

void DocumentWorkspace::activateCurrentDocument()
{
    auto* document = currentDocument();
    mux_.setCurrentDocument( document );
    quickFind_.registerSelector( document );
    Q_EMIT currentDocumentChanged( document );
}

void DocumentWorkspace::refreshQuickFindSelector()
{
    quickFind_.registerSelector( currentDocument() );
}

CrawlerWidget* DocumentWorkspace::openDocument( const QString& path, const QString& viewContext )
{
    if ( auto* existing = static_cast<CrawlerWidget*>( session_.getViewIfOpen( path ) ) ) {
        if ( indexOf( existing ) < 0 )
            return nullptr; // Another window owns this page; the host activates it.
        setCurrentWidget( existing );
        return existing;
    }
    auto* document
        = static_cast<CrawlerWidget*>( session_.open( path, [] { return new CrawlerWidget(); } ) );
    if ( !document )
        return nullptr;
    document->hide();
    if ( !viewContext.isEmpty() )
        document->setViewContext( viewContext );
    const int index = addCrawler( document, path );
    setCurrentIndex( index );
    return document;
}

QString DocumentWorkspace::closeDocument( int index )
{
    auto* document = qobject_cast<CrawlerWidget*>( widget( index ) );
    if ( !document )
        return {};
    const QString path = session_.getFilename( document );
    document->stopLoading();
    removeCrawler( index );
    session_.close( document );
    document->deleteLater();
    return path;
}

QList<CrawlerWidget*> DocumentWorkspace::restoreDocuments()
{
    int activeIndex = -1;
    const auto restored = session_.restore( [] { return new CrawlerWidget(); }, &activeIndex );
    QList<CrawlerWidget*> documents;
    for ( const auto& [ path, view ] : restored ) {
        if ( auto* document = static_cast<CrawlerWidget*>( view ) ) {
            addCrawler( document, path );
            documents.append( document );
        }
    }
    if ( activeIndex >= 0 )
        setCurrentIndex( activeIndex );
    return documents;
}

void DocumentWorkspace::saveDocuments( const QByteArray& geometry )
{
    std::vector<SaveFileInfo> views;
    for ( int index = 0; index < count(); ++index ) {
        auto* document = qobject_cast<CrawlerWidget*>( widget( index ) );
        views.emplace_back( document, 0UL, document->context() );
    }
    session_.save( views, geometry );
}
