#include "mainwindow.h"
#include "mainwindowtext.h"
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

void MainWindow::createToolBars()
{
    infoLine = new PathLine();
    infoLine->setObjectName( QStringLiteral( "mainInfoLine" ) );
    infoLine->setFrameStyle( QFrame::StyledPanel );
    infoLine->setFrameShadow( QFrame::Sunken );
    infoLine->setLineWidth( 0 );
    infoLine->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

    toolBar = addToolBar( QApplication::translate( "klogg::mainwindow::toolbar",
                                                   klogg::mainwindow::toolbar::toolbarTitle ) );
    toolBar->setIconSize( QSize( 16, 16 ) );
    toolBar->setMovable( false );
    toolBar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    toolBar->addAction( openAction );
    toolBar->addAction( reloadAction );
    toolBar->addAction( followAction );
    toolBar->addAction( addToFavoritesAction );
    toolBar->addWidget( infoLine );
    toolBar->addAction( stopAction );
    toolBar->addAction( showScratchPadAction );

    // VSCode-style status bar: file metadata on the left of the cluster,
    // editor state on the right, language selector rightmost.
    auto* status = statusBar();
    status->setObjectName( QStringLiteral( "mainStatusBar" ) );

    sizeField = new QLabel();
    sizeField->setObjectName( QStringLiteral( "sizeField" ) );
    sizeField->setContentsMargins( 6, 0, 6, 0 );

    dateField = new QLabel();
    dateField->setObjectName( QStringLiteral( "dateField" ) );
    dateField->setContentsMargins( 6, 0, 6, 0 );

    lineNbField = new QLabel();
    lineNbField->setObjectName( QStringLiteral( "lineNumberField" ) );
    lineNbField->setContentsMargins( 6, 0, 6, 0 );

    encodingField = new QLabel();
    encodingField->setObjectName( QStringLiteral( "encodingField" ) );
    encodingField->setContentsMargins( 6, 0, 6, 0 );

    lineEndingField = new QLabel();
    lineEndingField->setObjectName( QStringLiteral( "lineEndingField" ) );
    lineEndingField->setContentsMargins( 6, 0, 6, 0 );

    languageButton = new QToolButton();
    languageButton->setObjectName( QStringLiteral( "languageButton" ) );
    languageButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
    languageButton->setAutoRaise( true );
    // Shares the View > Syntax highlighting actions, so menu and status bar
    // never disagree about the current language.
    auto* languageMenu = new QMenu( languageButton );
    languageMenu->addActions( syntaxMenu->actions() );
    connect( languageMenu, &QMenu::aboutToShow, this, [ this, languageMenu ] {
        const auto* crawler = currentCrawlerWidget();
        for ( auto* action : languageMenu->actions() )
            action->setChecked( crawler && action->data().toString() == crawler->syntaxLanguage() );
    } );
    languageButton->setMenu( languageMenu );
    languageButton->setPopupMode( QToolButton::InstantPopup );

    status->addPermanentWidget( sizeField );
    status->addPermanentWidget( dateField );
    status->addPermanentWidget( lineNbField );
    status->addPermanentWidget( encodingField );
    status->addPermanentWidget( lineEndingField );
    status->addPermanentWidget( languageButton );

    showInfoLabels( false );
}
