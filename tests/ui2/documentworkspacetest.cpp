#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

#include "configuration.h"
#include "crawlerwidget.h"
#include "documentworkspace.h"
#include "quickfindmux.h"
#include "savedsearches.h"
#include "session.h"
#include "sessioninfo.h"
#include "signalmux.h"
#include "storagecontext.h"

class DocumentWorkspaceTest final : public QObject {
    Q_OBJECT
Q_SIGNALS:
    void followRequested( bool enabled );
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY( settings_.isValid() );
        QVERIFY( StorageContext::install( { StorageMode::CustomDirectory, settings_.path(),
                                            settings_.filePath( "storage.ini" ), true } ) );
        Configuration::getSynced();
        SavedSearches::getSynced();
        SessionInfo::getSynced();
    }

    void closesInactiveAndLastDocumentThenReopens()
    {
        QTemporaryDir files;
        const auto a = makeLog( files, "a.log" );
        const auto b = makeLog( files, "b.log" );
        QVERIFY( !a.isEmpty() );
        QVERIFY( !b.isEmpty() );
        auto session = std::make_shared<Session>();
        WindowSession window( session, "close-workspace", 0 );
        SignalMux mux;
        QuickFindMux quickFind( window.getQuickFindPattern() );
        DocumentWorkspace workspace( window, mux, quickFind );
        QPointer<CrawlerWidget> first = workspace.openDocument( a );
        QPointer<CrawlerWidget> second = workspace.openDocument( b );
        QVERIFY( first );
        QVERIFY( second );
        QCOMPARE( workspace.currentDocument(), second.data() );
        QCOMPARE( workspace.closeDocument( 0 ), a );
        QCOMPARE( workspace.currentDocument(), second.data() );
        QVERIFY( !session->getViewIfOpen( a ) );
        QCOMPARE( workspace.closeDocument( 0 ), b );
        QCOMPARE( workspace.count(), 0 );
        QVERIFY( !workspace.currentDocument() );
        QVERIFY( window.openedFiles().empty() );
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        QVERIFY( first.isNull() );
        QVERIFY( second.isNull() );
        quickFind.searchForward();
        QCOMPARE( workspace.closeDocument( -1 ), QString{} );
        QVERIFY( workspace.openDocument( a ) );
        QCOMPARE( workspace.count(), 1 );
        workspace.closeDocument( 0 );
    }

    void routesActionsOnlyToActiveDocument()
    {
        QTemporaryDir files;
        auto session = std::make_shared<Session>();
        WindowSession window( session, "route-workspace", 0 );
        SignalMux mux;
        QuickFindMux quickFind( window.getQuickFindPattern() );
        DocumentWorkspace workspace( window, mux, quickFind );
        mux.connect( this, SIGNAL( followRequested( bool ) ), SIGNAL( followSet( bool ) ) );
        auto* first = workspace.openDocument( makeLog( files, "a.log" ) );
        auto* second = workspace.openDocument( makeLog( files, "b.log" ) );
        QVERIFY( first );
        QVERIFY( second );
        QSignalSpy firstFollow( first, &CrawlerWidget::followSet );
        QSignalSpy secondFollow( second, &CrawlerWidget::followSet );
        Q_EMIT followRequested( true );
        QCOMPARE( firstFollow.count(), 0 );
        QCOMPARE( secondFollow.count(), 1 );
        workspace.setCurrentIndex( 0 );
        Q_EMIT followRequested( false );
        QCOMPARE( firstFollow.count(), 1 );
        QCOMPARE( secondFollow.count(), 1 );
        workspace.closeDocument( 0 );
        workspace.closeDocument( 0 );
        Q_EMIT followRequested( true );
        QCOMPARE( firstFollow.count(), 1 );
        QCOMPARE( secondFollow.count(), 1 );
    }

    void destroyingNonemptyWorkspaceUnregistersDocuments()
    {
        QTemporaryDir files;
        const auto path = makeLog(files, "destruction.log");
        auto session = std::make_shared<Session>();
        WindowSession window(session, "destroy-workspace", 0);
        SignalMux mux;
        QuickFindMux quickFind(window.getQuickFindPattern());
        QPointer<CrawlerWidget> document;
        {
            DocumentWorkspace workspace(window, mux, quickFind);
            document = workspace.openDocument(path);
            QVERIFY(document);
        }
        QVERIFY(document.isNull());
        QVERIFY(!session->getViewIfOpen(path));
        QVERIFY(window.openedFiles().empty());
        quickFind.searchForward();
        DocumentWorkspace next(window, mux, quickFind);
        QVERIFY(next.openDocument(path));
    }

    void preservesReorderedSession()
    {
        QTemporaryDir files;
        const auto a = makeLog( files, "a.log" );
        const auto b = makeLog( files, "b.log" );
        const QString id = "ordered-workspace";
        {
            auto session = std::make_shared<Session>();
            WindowSession window( session, id, 0 );
            SignalMux mux;
            QuickFindMux quickFind( window.getQuickFindPattern() );
            DocumentWorkspace workspace( window, mux, quickFind );
            QVERIFY( workspace.openDocument( a ) );
            QVERIFY( workspace.openDocument( b ) );
            workspace.findChild<CrawlerTabBar*>()->moveTab( 1, 0 );
            workspace.saveDocuments( QByteArray( "geometry" ) );
            const auto saved = SessionInfo::get().openFiles( id );
            QCOMPARE( saved.size(), size_t( 2 ) );
            QCOMPARE( saved[ 0 ].fileName, b );
            QCOMPARE( saved[ 1 ].fileName, a );
            workspace.closeDocument( 0 );
            workspace.closeDocument( 0 );
        }
        auto session = std::make_shared<Session>();
        WindowSession window( session, id, 0 );
        SignalMux mux;
        QuickFindMux quickFind( window.getQuickFindPattern() );
        DocumentWorkspace workspace( window, mux, quickFind );
        QCOMPARE( workspace.restoreDocuments().size(), qsizetype( 2 ) );
        QCOMPARE( window.getFilename( qobject_cast<CrawlerWidget*>( workspace.widget( 0 ) ) ), b );
        QCOMPARE( window.getFilename( qobject_cast<CrawlerWidget*>( workspace.widget( 1 ) ) ), a );
        workspace.closeDocument( 0 );
        workspace.closeDocument( 0 );
    }

private:
    static QString makeLog( const QTemporaryDir& dir, const char* name )
    {
        QFile file( dir.filePath( name ) );
        if ( !file.open( QIODevice::WriteOnly ) || file.write( "first line\nsecond line\n" ) < 0 )
            return {};
        return file.fileName();
    }
    QTemporaryDir settings_;
};

QTEST_MAIN( DocumentWorkspaceTest )
#include "documentworkspacetest.moc"
