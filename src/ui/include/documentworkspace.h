#pragma once

#include "tabbedcrawlerwidget.h"

class CrawlerWidget;
class WindowSession;
class SignalMux;
class QuickFindMux;

// Document-level operations over the existing tab UI. The borrowed session and
// routers must outlive this widget; MainWindow owns them before this member.
class DocumentWorkspace final : public TabbedCrawlerWidget {
    Q_OBJECT
public:
    DocumentWorkspace( WindowSession& session, SignalMux& mux, QuickFindMux& quickFind );
    ~DocumentWorkspace() override;

    CrawlerWidget* currentDocument() const;
    CrawlerWidget* openDocument( const QString& path, const QString& viewContext = {} );
    QString closeDocument( int index );
    QList<CrawlerWidget*> restoreDocuments();
    void saveDocuments( const QByteArray& geometry );
    void refreshQuickFindSelector();

Q_SIGNALS:
    void currentDocumentChanged( CrawlerWidget* document );

private:
    void activateCurrentDocument();
    WindowSession& session_;
    SignalMux& mux_;
    QuickFindMux& quickFind_;
};
