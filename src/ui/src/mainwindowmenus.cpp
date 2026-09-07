#include "mainwindow.h"
#include "mainwindowtext.h"
#include "encodings.h"
#include "highlightersmenu.h"
#include <QMenuBar>
#include <QActionGroup>

void MainWindow::createMenus(QMenuBar& container)
{
    using namespace klogg::mainwindow;

    fileMenu = container.addMenu( tr( menu::fileTitle ) );
    fileMenu->setToolTipsVisible( true );
    fileMenu->addAction( newWindowAction );
    fileMenu->addAction( openAction );
    fileMenu->addAction( openClipboardAction );
    fileMenu->addAction( openUrlAction );
    recentFilesMenu = fileMenu->addMenu( tr( "Open Recent" ) );
    recentFilesMenu->setObjectName( QStringLiteral( "recentFilesMenu" ) );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFilesMenu->addAction( recentFileActions[ i ] );
    }
    recentFilesMenu->addSeparator();
    recentFilesMenu->addAction( recentFilesCleanup );
    recentFilesMenu->setEnabled( false );
    fileMenu->addSeparator();

    fileMenu->addAction( closeAction );
    fileMenu->addAction( closeAllAction );
    fileMenu->addSeparator();

    fileMenu->addAction( optionsAction );
    fileMenu->addSeparator();

    fileMenu->addSeparator();
    fileMenu->addAction( exitAction );

    editMenu = container.addMenu( tr( menu::editTitle ) );
    editMenu->addAction( copyAction );
    editMenu->addAction( selectAllAction );
    editMenu->addSeparator();
    editMenu->addAction( findAction );
    editMenu->addSeparator();
    editMenu->addAction( goToLineAction );
    editMenu->addSeparator();
    editMenu->addAction( copyPathToClipboardAction );
    editMenu->addAction( openContainingFolderAction );
    editMenu->addSeparator();
    editMenu->addAction( openInEditorAction );
    editMenu->addAction( clearLogAction );
    editMenu->setEnabled( false );

    viewMenu = container.addMenu( tr( menu::viewTitle ) );
    openedFilesMenu = viewMenu->addMenu( tr( menu::openedFilesTitle ) );
    viewMenu->addSeparator();
    viewMenu->addAction( overviewVisibleAction );
    viewMenu->addSeparator();
    viewMenu->addAction( lineNumbersVisibleAction );
    viewMenu->addSeparator();
    viewMenu->addAction( textWrapAction );
    viewMenu->addSeparator();
    viewMenu->addAction( followAction );
    viewMenu->addSeparator();
    viewMenu->addAction( reloadAction );

    toolsMenu = container.addMenu( tr( menu::toolsTitle ) );

    highlightersMenu = new HighlightersMenu( tr( menu::highlightersTitle ), &container );
    container.addMenu( highlightersMenu );
    highlightersMenu->setApplyChange( [ this ]() {
        auto crawler = currentCrawlerWidget();
        if ( crawler != nullptr ) {
            crawler->applyConfiguration();
        }
    } );

    toolsMenu->addAction( predefinedFiltersDialogAction );

    toolsMenu->addSeparator();
    toolsMenu->addAction( showScratchPadAction );

    encodingMenu = EncodingMenu::generate( encodingGroup, &container );
    container.addMenu( encodingMenu );
    container.addSeparator();

    favoritesMenu = container.addMenu( tr( menu::favoritesTitle ) );
    favoritesMenu->setToolTipsVisible( true );

    helpMenu = container.addMenu( tr( menu::helpTitle ) );
    helpMenu->addAction( showDocumentationAction );
    helpMenu->addSeparator();
    helpMenu->addAction( reportIssueAction );
    helpMenu->addSeparator();
    helpMenu->addAction( generateDumpAction );
    helpMenu->addSeparator();
    helpMenu->addAction( aboutQtAction );
    helpMenu->addAction( aboutAction );
}
