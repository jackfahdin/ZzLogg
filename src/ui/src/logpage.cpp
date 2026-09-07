#include "logpage.h"
#include "searchpanel.h"
#include <QTabWidget>
#include <QVBoxLayout>

LogPage::LogPage( QWidget* parent )
    : QSplitter( Qt::Vertical, parent )
{
}

void LogPage::compose( QWidget* mainView, SearchPanel* searchPanel, QWidget* firstResults )
{
    Q_ASSERT( !resultsTabs_ );
    auto* bottomWindow = new QWidget;
    bottomWindow->setContentsMargins( 2, 0, 2, 0 );
    resultsTabs_ = new QTabWidget;
    resultsTabs_->setObjectName( QStringLiteral( "filteredResultsTabs" ) );
    resultsTabs_->setTabsClosable( true );
    resultsTabs_->addTab( firstResults, "" );
    resultsTabs_->setDocumentMode( true );
    resultsTabs_->setTabBarAutoHide( true );
    auto* layout = new QVBoxLayout( bottomWindow );
    layout->addWidget( searchPanel );
    layout->addWidget( resultsTabs_ );
    layout->setContentsMargins( 2, 2, 2, 2 );
    addWidget( mainView );
    addWidget( bottomWindow );
}
