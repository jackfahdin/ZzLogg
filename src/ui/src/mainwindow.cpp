/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements MainWindow. It is responsible for creating and
// managing the menus, the toolbar, and the CrawlerWidget. It also
// load/save the settings on opening/closing of the app

#include "configuration.h"
#include "containers.h"
#include "log.h"
#include <QNetworkReply>
#include <cassert>
#include <exception>

#include <iterator>
#include <qaction.h>
#include <qapplication.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // Q_OS_WIN

#include <QClipboard>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QResource>
#include <QScreen>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolTip>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>

#include "mainwindow.h"
#include "aboutdialog.h"
#include "documentationwindow.h"
#include "windowchrome.h"

#include "clipboard.h"
#include "crawlerwidget.h"
#include "decompressor.h"
#include "dispatch_to.h"
#include "downloader.h"
#include "encodings.h"
#include "favoritefiles.h"
#include "highlightersdialog.h"
#include "highlightersmenu.h"
#include "issuereporter.h"
#include "klogg_version.h"
#include "logger.h"
#include "mainwindowtext.h"
#include "openfilehelper.h"
#include "optionsdialog.h"
#include "predefinedfiltersdialog.h"
#include "progress.h"
#include "readablesize.h"
#include "recentfiles.h"
#include "sessioninfo.h"
#include "shortcuts.h"
#include "styles.h"
#include "tabbedcrawlerwidget.h"
#include "zzlogg_brand.h"

namespace {

void signalCrawlerToFollowFile( CrawlerWidget* crawler_widget )
{
    dispatchToMainThread( [ crawler_widget ]() { crawler_widget->followSet( true ); } );
}

static constexpr auto ClipboardMaxTry = 5;

QString productName()
{
    return QString::fromLatin1( zzlogg::brand::ProductName );
}

} // namespace

std::unique_ptr<QTranslator> MainWindow::mTranslator;
std::unique_ptr<QTranslator> MainWindow::mQtTranslator;

MainWindow::MainWindow( WindowSession session )
    : MainWindow(std::move(session), static_cast<const UiThemeContext*>(nullptr)) {}

MainWindow::MainWindow(WindowSession session, UiThemeContext context)
    : MainWindow(std::move(session), &context) {}

MainWindow::~MainWindow() { chrome_.reset(); }

MainWindow::MainWindow(WindowSession session, const UiThemeContext* context)
    : session_( std::move( session ) )
    , mainIcon_()
    , iconLoader_( this )
    , signalMux_()
    , quickFindMux_( session_.getQuickFindPattern() )
    , workspace_( session_, signalMux_, quickFindMux_ )
    , tempDir_( QDir::temp().filePath( "klogg_temp_" ) )
{
    if (context) {
        chrome_ = std::make_unique<WindowChrome>(*this, *context);
        connect(this, &MainWindow::activeDocumentNameChanged,
                chrome_.get(), &WindowChrome::setDocumentName);
    }
    createActions();
    createMenus(chrome_ ? chrome_->commandMenuBar() : *menuBar());
    createToolBars();

    setAcceptDrops( true );

    // Default geometry
    const QRect geometry = QApplication::primaryScreen()->availableGeometry();
    setGeometry( geometry.x() + 20, geometry.y() + 40, geometry.width() - 140,
                 geometry.height() - 140 );

    mainIcon_ = QApplication::windowIcon();
    setWindowIcon( mainIcon_ );
    readSettings();

    createTrayIcon();

    // Connect the signals to the mux (they will be forwarded to the
    // "current" crawlerwidget

    // Send actions to the crawlerwidget
    signalMux_.connect( this, SIGNAL( followSet( bool ) ), SIGNAL( followSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( textWrapSet( bool ) ), SIGNAL( textWrapSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
    signalMux_.connect( this, SIGNAL( enteringQuickFind() ), SLOT( enteringQuickFind() ) );
    signalMux_.connect( &quickFindWidget_, SIGNAL( close() ), SLOT( exitingQuickFind() ) );

    // Actions from the CrawlerWidget
    signalMux_.connect( SIGNAL( followModeChanged( bool ) ), this,
                        SLOT( changeFollowMode( bool ) ) );
    signalMux_.connect(
        SIGNAL( newSelection( LineNumber, LinesCount, LineColumn, LineLength ) ), this,
        SLOT( lineNumberHandler( LineNumber, LinesCount, LineColumn, LineLength ) ) );
    signalMux_.connect( SIGNAL( saveCurrentSearchAsPredefinedFilter( QString ) ), this,
                        SLOT( newPredefinedFilterHandler( QString ) ) );

    signalMux_.connect( SIGNAL( sendToScratchpad( QString ) ), this,
                        SLOT( sendToScratchpad( QString ) ) );

    signalMux_.connect( SIGNAL( replaceDataInScratchpad( QString ) ), this,
                        SLOT( replaceDataInScratchpad( QString ) ) );

    // Register for progress status bar
    signalMux_.connect( SIGNAL( loadingProgressed( int ) ), this,
                        SLOT( updateLoadingProgress( int ) ) );
    signalMux_.connect( SIGNAL( loadingFinished( LoadingStatus ) ), this,
                        SLOT( handleLoadingFinished( LoadingStatus ) ) );

    signalMux_.connect( SIGNAL( filteredViewChanged() ), this,
                        SLOT( handleFilteredViewChanged() ) );
    signalMux_.connect( SIGNAL( languageDisplayChanged() ), this,
                        SLOT( retranslateStatusUi() ) );

    scratchPad_.setWindowIcon( mainIcon_ );
    scratchPad_.setWindowTitle( tr( "%1 - scratchpad" ).arg( productName() ) );

    connect( &workspace_, &TabbedCrawlerWidget::tabCloseRequested, this,
             [ this ]( int index ) { this->closeTab( index, ActionInitiator::User ); } );
    connect( &workspace_, &DocumentWorkspace::currentDocumentChanged, this,
             &MainWindow::currentDocumentChanged );

    // Establish the QuickFindWidget and mux ( to send requests from the
    // QFWidget to the right window )
    connect( &quickFindWidget_, SIGNAL( patternConfirmed( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( confirmPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( patternUpdated( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( setNewPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( cancelSearch() ), &quickFindMux_, SLOT( cancelSearch() ) );
    connect( &quickFindWidget_, SIGNAL( searchForward() ), &quickFindMux_,
             SLOT( searchForward() ) );
    connect( &quickFindWidget_, SIGNAL( searchBackward() ), &quickFindMux_,
             SLOT( searchBackward() ) );
    connect( &quickFindWidget_, SIGNAL( searchNext() ), &quickFindMux_, SLOT( searchNext() ) );

    // QuickFind changes coming from the views
    connect( &quickFindMux_, SIGNAL( patternChanged( const QString& ) ), this,
             SLOT( changeQFPattern( const QString& ) ) );
    connect( &quickFindMux_, SIGNAL( notify( const QFNotification& ) ), &quickFindWidget_,
             SLOT( notify( const QFNotification& ) ) );
    connect( &quickFindMux_, SIGNAL( clearNotification() ), &quickFindWidget_,
             SLOT( clearNotification() ) );

    // Construct the QuickFind bar
    quickFindWidget_.hide();

    QWidget* central_widget = new QWidget();
    auto* main_layout = new QVBoxLayout();
    main_layout->setContentsMargins( 0, 0, 0, 0 );
    main_layout->addWidget( &workspace_ );
    main_layout->addWidget( &quickFindWidget_ );
    central_widget->setLayout( main_layout );

    setCentralWidget( central_widget );

    updateTitleBar( "" );
    loadIcons();
    reTranslateUI();
}

void MainWindow::reloadGeometry()
{
    QByteArray geometry;

    session_.restoreGeometry( &geometry );
    restoreGeometry( geometry );
}

void MainWindow::reloadSession()
{
    if ( applicationExitPrepared_ ) return;
    const auto& config = Configuration::get();
    const auto followFileOnLoad = config.followFileOnLoad() && config.anyFileWatchEnabled();

    const auto documents = workspace_.restoreDocuments();
    if ( followFileOnLoad ) {
        for ( auto* document : documents )
            signalCrawlerToFollowFile( document );
        if ( !documents.isEmpty() )
            followAction->setChecked( true );
    }

    updateOpenedFilesMenu();
}

void MainWindow::loadInitialFile( QString fileName, bool followFile )
{
    LOG_DEBUG << "loadInitialFile";

    // Is there a file passed as argument?
    if ( !fileName.isEmpty() ) {
        loadFile( fileName, followFile );
    }
}

void MainWindow::reTranslateUI()
{
    using namespace klogg::mainwindow;
    // menu
    auto transMenu = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::menu", text );
    };
    fileMenu->setTitle( transMenu( menu::fileTitle ) );
    editMenu->setTitle( transMenu( menu::editTitle ) );
    viewMenu->setTitle( transMenu( menu::viewTitle ) );
    openedFilesMenu->setTitle( transMenu( menu::openedFilesTitle ) );
    toolsMenu->setTitle( transMenu( menu::toolsTitle ) );
    highlightersMenu->setTitle( transMenu( menu::highlightersTitle ) );
    favoritesMenu->setTitle( transMenu( menu::favoritesTitle ) );
    helpMenu->setTitle( transMenu( menu::helpTitle ) );
    recentFilesMenu->setTitle( tr( "Open Recent" ) );
    EncodingMenu::retranslate( encodingMenu );

    // toolbar
    toolBar->setToolTip(
        QApplication::translate( "klogg::mainwindow::toolbar", toolbar::toolbarTitle ) );

    // action
    auto transAction = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::action", text );
    };
    newWindowAction->setText( transAction( action::newWindowText ) );
    newWindowAction->setStatusTip(
        transAction( action::newWindowStatusTip ).arg( productName() ) );

    openAction->setText( transAction( action::openText ) );
    openAction->setStatusTip( transAction( action::openStatusTip ) );

    recentFilesCleanup->setText( transAction( action::recentFilesCleanupText ) );

    closeAction->setText( transAction( action::closeText ) );
    closeAction->setStatusTip( transAction( action::closeStatusTip ) );

    closeAllAction->setText( transAction( action::closeAllText ) );
    closeAllAction->setStatusTip( transAction( action::closeAllStatusTip ) );

    exitAction->setText( transAction( action::exitText ) );
    exitAction->setStatusTip( transAction( action::exitStatusTip ) );

    copyAction->setText( transAction( action::copyText ) );
    copyAction->setStatusTip( transAction( action::copyStatusTip ) );

    selectAllAction->setText( transAction( action::selectAllText ) );
    selectAllAction->setStatusTip( transAction( action::selectAllStatusTip ) );

    goToLineAction->setText( transAction( action::goToLineText ) );
    goToLineAction->setStatusTip( transAction( action::goToLineStatusTip ) );

    findAction->setText( transAction( action::findText ) );
    findAction->setStatusTip( transAction( action::findStatusTip ) );

    clearLogAction->setText( transAction( action::clearLogText ) );
    clearLogAction->setStatusTip( transAction( action::clearLogStatusTip ) );

    openContainingFolderAction->setText( transAction( action::openContainingFolderText ) );
    openContainingFolderAction->setStatusTip(
        transAction( action::openContainingFolderStatusTip ) );

    openInEditorAction->setText( transAction( action::openInEditorText ) );
    openInEditorAction->setStatusTip( transAction( action::openInEditorStatusTip ) );

    copyPathToClipboardAction->setText( transAction( action::copyPathToClipboardText ) );
    copyPathToClipboardAction->setStatusTip( transAction( action::copyPathToClipboardStatusTip ) );

    openClipboardAction->setText( transAction( action::openClipboardText ) );
    openClipboardAction->setStatusTip( transAction( action::openClipboardStatusTip ) );

    openUrlAction->setText( transAction( action::openUrlText ) );
    openUrlAction->setStatusTip( transAction( action::openUrlStatusTip ) );

    overviewVisibleAction->setText( transAction( action::overviewVisibleText ) );

    lineNumbersVisibleAction->setText( transAction( action::lineNumbersVisibleText ) );

    followAction->setText( transAction( action::followText ) );
    textWrapAction->setText( transAction( action::wrapText ) );
    reloadAction->setText( transAction( action::reloadText ) );
    stopAction->setText( transAction( action::stopText ) );

    optionsAction->setText( transAction( action::optionsText ) );
    optionsAction->setStatusTip( transAction( action::optionsStatusTip ) );

    editHighlightersAction->setText( transAction( action::editHighlightersText ) );
    editHighlightersAction->setStatusTip( transAction( action::editHighlightersStatusTip ) );

    showDocumentationAction->setText( transAction( action::showDocumentationText ) );
    showDocumentationAction->setStatusTip( transAction( action::showDocumentationStatusTip ) );

    aboutAction->setText( transAction( action::aboutText ) );
    checkUpdatesAction->setText(tr("Check for updates"));
    aboutAction->setStatusTip( transAction( action::aboutStatusTip ) );

    aboutQtAction->setText( transAction( action::aboutQtText ) );
    aboutQtAction->setStatusTip( transAction( action::aboutQtStatusTip ) );

    reportIssueAction->setText( transAction( action::reportIssueText ) );
    reportIssueAction->setStatusTip( transAction( action::reportIssueStatusTip ) );


    showScratchPadAction->setText( transAction( action::showScratchPadText ) );
    showScratchPadAction->setStatusTip( transAction( action::showScratchPadStatusTip ) );

    auto curFavoritesIconText = addToFavoritesAction->data().toBool()
                                    ? transAction( action::addToFavoritesText )
                                    : transAction( action::removeFromFavoritesText );
    addToFavoritesAction->setText( curFavoritesIconText );
    addToFavoritesMenuAction->setText( transAction( action::addToFavoritesText ) );

    removeFromFavoritesAction->setText( transAction( action::removeFromFavoritesText ) );

    selectOpenFileAction->setText( transAction( action::selectOpenFileText ) );

    predefinedFiltersDialogAction->setText( transAction( action::predefinedFiltersDialogText ) );
    predefinedFiltersDialogAction->setStatusTip(
        transAction( action::predefinedFiltersDialogStatusTip ) );

    // trayIcon
    trayOpenAction->setText( tr( "Open window" ) );
    trayQuitAction->setText( tr( "Quit" ) );
    trayIcon_->setToolTip( QApplication::translate( "klogg::mainwindow::trayicon",
                                                    klogg::mainwindow::trayicon::trayiconTip )
                               .arg( productName() ) );

    scratchPad_.setWindowTitle( tr( "%1 - scratchpad" ).arg( productName() ) );
    auto* const crawler = currentCrawlerWidget();
    updateTitleBar( crawler ? session_.getFilename( crawler ) : QString{} );
}

int MainWindow::installLanguage( QString lang )
{
    if ( lang.isEmpty() ) {
        return -1;
    }

    QString appPath( ":/i18n/" + lang + ".qm" );
    QResource appTranslations( appPath );
    auto appCandidate = std::make_unique<QTranslator>();
    if ( !appTranslations.isValid() || !appCandidate->load( appPath ) ) {
        LOG_ERROR << "Failed to load application translator" << appPath;
        return -1;
    }

    auto qtCandidate = std::unique_ptr<QTranslator>{};
    QString qtPath( ":/i18n/qt_" + lang + ".qm" );
    QResource qtTranslations( qtPath );
    if ( qtTranslations.isValid() ) {
        qtCandidate = std::make_unique<QTranslator>();
        if ( !qtCandidate->load( qtPath ) ) {
            LOG_WARNING << "Failed to load optional Qt translator" << qtPath;
            qtCandidate.reset();
        }
    }

    bool qtCandidateInstalled = false;
    if ( qtCandidate != nullptr ) {
        qtCandidateInstalled = QApplication::installTranslator( qtCandidate.get() );
        if ( !qtCandidateInstalled ) {
            LOG_WARNING << "Failed to install optional Qt translator" << qtPath;
            qtCandidate.reset();
        }
    }
    if ( !QApplication::installTranslator( appCandidate.get() ) ) {
        if ( qtCandidateInstalled ) {
            QApplication::removeTranslator( qtCandidate.get() );
        }
        LOG_ERROR << "Failed to install application translator" << appPath;
        return -1;
    }

    if ( mTranslator != nullptr ) {
        QApplication::removeTranslator( mTranslator.get() );
    }
    if ( mQtTranslator != nullptr ) {
        QApplication::removeTranslator( mQtTranslator.get() );
    }
    mTranslator = std::move( appCandidate );
    mQtTranslator = std::move( qtCandidate );

    return 0;
}

// Menu actions
void MainWindow::createActions()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    using namespace klogg::mainwindow;

    newWindowAction = new QAction( tr( action::newWindowText ), this );
    newWindowAction->setStatusTip( tr( action::newWindowStatusTip ).arg( productName() ) );
    connect( newWindowAction, &QAction::triggered, [ this ] { Q_EMIT newWindow(); } );
    newWindowAction->setVisible( config.allowMultipleWindows() );

    openAction = new QAction( tr( action::openText ), this );
    openAction->setStatusTip( tr( action::openStatusTip ) );
    connect( openAction, &QAction::triggered, [ this ]( auto ) { this->open(); } );

    recentFilesCleanup = new QAction( tr( action::recentFilesCleanupText ), this );
    connect( recentFilesCleanup, &QAction::triggered, this,
             [ this ]( auto ) { this->clearRecentFileActions(); } );

    closeAction = new QAction( tr( action::closeText ), this );
    closeAction->setStatusTip( tr( action::closeStatusTip ) );
    connect( closeAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeTab( ActionInitiator::User ); } );

    closeAllAction = new QAction( tr( action::closeAllText ), this );
    closeAllAction->setStatusTip( tr( action::closeAllStatusTip ) );
    connect( closeAllAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeAll( ActionInitiator::User ); } );

    recentFilesGroup = new QActionGroup( this );
    connect( recentFilesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromRecent );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFileActions[ i ] = new QAction( this );
        connect( recentFileActions[ i ], &QAction::hovered, [ this, a = recentFileActions[ i ] ]() {
            QToolTip::showText( QCursor::pos(), a->toolTip(), this );
        } );
        recentFileActions[ i ]->setVisible( false );
        recentFileActions[ i ]->setActionGroup( recentFilesGroup );
    }

    exitAction = new QAction( tr( action::exitText ), this );
    exitAction->setStatusTip( tr( action::exitStatusTip ) );
    connect( exitAction, &QAction::triggered, this, &MainWindow::exitRequested );

    copyAction = new QAction( tr( action::copyText ), this );
    copyAction->setStatusTip( tr( action::copyStatusTip ) );
    connect( copyAction, &QAction::triggered, this, [ this ]( auto ) { this->copy(); } );

    selectAllAction = new QAction( tr( action::selectAllText ), this );
    selectAllAction->setStatusTip( tr( action::selectAllStatusTip ) );
    connect( selectAllAction, &QAction::triggered, this, [ this ]( auto ) { this->selectAll(); } );

    goToLineAction = new QAction( tr( action::goToLineText ), this );
    goToLineAction->setStatusTip( tr( action::goToLineStatusTip ) );
    signalMux_.connect( goToLineAction, SIGNAL( triggered() ), SLOT( goToLine() ) );

    findAction = new QAction( tr( action::findText ), this );
    findAction->setStatusTip( tr( action::findStatusTip ) );
    connect( findAction, &QAction::triggered, this, [ this ]( auto ) { this->find(); } );

    clearLogAction = new QAction( tr( action::clearLogText ), this );
    clearLogAction->setStatusTip( tr( action::clearLogStatusTip ) );
    connect( clearLogAction, &QAction::triggered, this, [ this ]( auto ) { this->clearLog(); } );

    openContainingFolderAction = new QAction( tr( action::openContainingFolderText ), this );
    openContainingFolderAction->setStatusTip( tr( action::openContainingFolderStatusTip ) );
    connect( openContainingFolderAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openContainingFolder(); } );

    openInEditorAction = new QAction( tr( action::openInEditorText ), this );
    openInEditorAction->setStatusTip( tr( action::openInEditorStatusTip ) );
    connect( openInEditorAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openInEditor(); } );

    copyPathToClipboardAction = new QAction( tr( action::copyPathToClipboardText ), this );
    copyPathToClipboardAction->setStatusTip( tr( action::copyPathToClipboardStatusTip ) );
    connect( copyPathToClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyFullPath(); } );

    openClipboardAction = new QAction( tr( action::openClipboardText ), this );
    openClipboardAction->setStatusTip( tr( action::openClipboardStatusTip ) );
    connect( openClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openClipboard(); } );

    openUrlAction = new QAction( tr( action::openUrlText ), this );
    openUrlAction->setStatusTip( tr( action::openUrlStatusTip ) );
    connect( openUrlAction, &QAction::triggered, this, [ this ]( auto ) { this->openUrl(); } );

    overviewVisibleAction = new QAction( tr( action::overviewVisibleText ), this );
    overviewVisibleAction->setCheckable( true );
    overviewVisibleAction->setChecked( config.isOverviewVisible() );
    connect( overviewVisibleAction, &QAction::toggled, this,
             &MainWindow::toggleOverviewVisibility );

    lineNumbersVisibleAction = new QAction( tr( action::lineNumbersVisibleText ), this );
    lineNumbersVisibleAction->setObjectName( QStringLiteral( "lineNumbersVisibleAction" ) );
    lineNumbersVisibleAction->setCheckable( true );
    lineNumbersVisibleAction->setChecked( config.lineNumbersVisible() );
    connect( lineNumbersVisibleAction, &QAction::toggled, this, [ this ]( bool visible ) {
        auto& configuration = Configuration::get();
        configuration.setLineNumbersVisible( visible );
        configuration.save();
        for ( int index = 0; index < workspace_.count(); ++index ) {
            auto* const crawler
                = qobject_cast<CrawlerWidget*>( workspace_.widget( index ) );
            if ( crawler != nullptr ) {
                crawler->applyConfiguration();
            }
        }
    } );

    followAction = new QAction( tr( action::followText ), this );
    followAction->setCheckable( true );
    followAction->setEnabled( config.anyFileWatchEnabled() );
    connect( followAction, &QAction::toggled, this, &MainWindow::followSet );

    textWrapAction = new QAction( tr( action::wrapText ), this );
    textWrapAction->setCheckable( true );
    textWrapAction->setEnabled( true );
    connect( textWrapAction, &QAction::toggled, this, &MainWindow::textWrapSet );

    reloadAction = new QAction( tr( action::reloadText ), this );
    signalMux_.connect( reloadAction, SIGNAL( triggered() ), SLOT( reload() ) );

    stopAction = new QAction( tr( action::stopText ), this );
    stopAction->setEnabled( true );
    signalMux_.connect( stopAction, SIGNAL( triggered() ), SLOT( stopLoading() ) );

    optionsAction = new QAction( tr( action::optionsText ), this );
    optionsAction->setMenuRole( QAction::PreferencesRole );
    optionsAction->setStatusTip( tr( action::optionsStatusTip ) );
    connect( optionsAction, &QAction::triggered, this, [ this ]( auto ) { this->options(); } );

    editHighlightersAction = new QAction( tr( action::editHighlightersText ), this );
    editHighlightersAction->setMenuRole( QAction::NoRole );
    editHighlightersAction->setStatusTip( tr( action::editHighlightersStatusTip ) );
    connect( editHighlightersAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editHighlighters(); } );

    showDocumentationAction = new QAction( tr( action::showDocumentationText ), this );
    showDocumentationAction->setStatusTip( tr( action::showDocumentationStatusTip ) );
    connect( showDocumentationAction, &QAction::triggered, this,
             [ this ]( auto ) { this->documentation(); } );

    aboutAction = new QAction( tr( action::aboutText ), this );
    checkUpdatesAction = new QAction(tr("Check for updates"), this);
    checkUpdatesAction->setObjectName("checkUpdatesAction");
    connect(checkUpdatesAction, &QAction::triggered, this, &MainWindow::checkUpdatesRequested);
    aboutAction->setStatusTip( tr( action::aboutStatusTip ) );
    connect( aboutAction, &QAction::triggered, this, [ this ]( auto ) { this->about(); } );

    aboutQtAction = new QAction( tr( action::aboutQtText ), this );
    aboutQtAction->setStatusTip( tr( action::aboutQtStatusTip ) );
    connect( aboutQtAction, &QAction::triggered, this, [ this ]( auto ) { this->aboutQt(); } );

    reportIssueAction = new QAction( tr( action::reportIssueText ), this );
    reportIssueAction->setStatusTip( tr( action::reportIssueStatusTip ) );
    connect( reportIssueAction, &QAction::triggered, this,
             []( auto ) { IssueReporter::reportIssue( IssueTemplate::Bug ); } );


    showScratchPadAction = new QAction( tr( action::showScratchPadText ), this );
    showScratchPadAction->setObjectName( QStringLiteral( "showScratchPadAction" ) );
    showScratchPadAction->setStatusTip( tr( action::showScratchPadStatusTip ) );
    connect( showScratchPadAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showScratchPad(); } );

    encodingGroup = new QActionGroup( this );
    connect( encodingGroup, &QActionGroup::triggered, this, &MainWindow::encodingChanged );

    favoritesGroup = new QActionGroup( this );
    connect( favoritesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromFavorites );

    openedFilesGroup = new QActionGroup( this );
    connect( openedFilesGroup, &QActionGroup::triggered, this, &MainWindow::switchToOpenedFile );

    addToFavoritesAction = new QAction( tr( action::addToFavoritesText ), this );
    addToFavoritesAction->setData( true );
    connect( addToFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    addToFavoritesMenuAction = new QAction( tr( action::addToFavoritesText ), this );
    connect( addToFavoritesMenuAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    removeFromFavoritesAction = new QAction( tr( action::removeFromFavoritesText ), this );
    connect( removeFromFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->removeFromFavorites(); } );

    selectOpenFileAction = new QAction( tr( action::selectOpenFileText ), this );
    connect( selectOpenFileAction, &QAction::triggered, this,
             [ this ]( auto ) { this->selectOpenedFile(); } );

    predefinedFiltersDialogAction = new QAction( tr( action::predefinedFiltersDialogText ), this );
    predefinedFiltersDialogAction->setStatusTip( tr( action::predefinedFiltersDialogStatusTip ) );
    connect( predefinedFiltersDialogAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editPredefinedFilters(); } );

    updateShortcuts();
}

void MainWindow::updateShortcuts()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfForward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Forward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfBackward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Backward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFocusSearchInput, [ this ] {
                                          if ( auto crawler = currentCrawlerWidget() ) {
                                              crawler->focusSearchEdit();
                                          }
                                      } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFullScreen,
                                      [ this ] { this->showFullScreen(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMax,
                                      [ this ] { this->showMaximized(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMin,
                                      [ this ] { this->showMinimized(); } );

    auto setShortcuts = [ &shortcuts ]( auto* action, const auto& actionName ) {
        action->setShortcuts( ShortcutAction::shortcutKeys( actionName, shortcuts ) );
    };

    setShortcuts( newWindowAction, ShortcutAction::MainWindowNewWindow );
    setShortcuts( openAction, ShortcutAction::MainWindowOpenFile );
    setShortcuts( closeAction, ShortcutAction::MainWindowCloseFile );
    setShortcuts( closeAllAction, ShortcutAction::MainWindowCloseAll );
    setShortcuts( exitAction, ShortcutAction::MainWindowQuit );
    setShortcuts( copyAction, ShortcutAction::MainWindowCopy );
    setShortcuts( selectAllAction, ShortcutAction::MainWindowSelectAll );
    setShortcuts( findAction, ShortcutAction::MainWindowOpenQf );
    setShortcuts( clearLogAction, ShortcutAction::MainWindowClearFile );
    setShortcuts( openContainingFolderAction, ShortcutAction::MainWindowOpenContainingFolder );
    setShortcuts( openInEditorAction, ShortcutAction::MainWindowOpenInEditor );
    setShortcuts( copyPathToClipboardAction, ShortcutAction::MainWindowCopyPathToClipboard );
    setShortcuts( openClipboardAction, ShortcutAction::MainWindowOpenFromClipboard );
    setShortcuts( openUrlAction, ShortcutAction::MainWindowOpenFromUrl );
    setShortcuts( followAction, ShortcutAction::MainWindowFollowFile );
    setShortcuts( textWrapAction, ShortcutAction::MainWindowTextWrap );
    setShortcuts( reloadAction, ShortcutAction::MainWindowReload );
    setShortcuts( stopAction, ShortcutAction::MainWindowStop );
    setShortcuts( showScratchPadAction, ShortcutAction::MainWindowScratchpad );
    setShortcuts( selectOpenFileAction, ShortcutAction::MainWindowSelectOpenFile );
    setShortcuts( goToLineAction, ShortcutAction::LogViewJumpToLine );
    setShortcuts( optionsAction, ShortcutAction::MainWindowPreference );
}

void MainWindow::loadIcons()
{
    openAction->setIcon( iconLoader_.load( "icons8-open-file" ) );
    stopAction->setIcon( iconLoader_.load( "icons8-delete" ) );
    reloadAction->setIcon( iconLoader_.load( "icons8-restore-page" ) );
    followAction->setIcon( iconLoader_.load( "icons8-fast-forward" ) );
    showScratchPadAction->setIcon( iconLoader_.load( "icons8-create" ) );
    const auto favoriteIconName = addToFavoritesAction->data().toBool()
                                      ? QStringLiteral( "icons8-star" )
                                      : QStringLiteral( "icons8-star-filled" );
    addToFavoritesAction->setIcon( iconLoader_.load( favoriteIconName ) );
    addToFavoritesMenuAction->setIcon( iconLoader_.load( favoriteIconName ) );
}


void MainWindow::createTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon( this );

    QMenu* trayMenu = new QMenu( this );
    trayOpenAction = new QAction( tr( "Open window" ), this );
    trayOpenAction->setObjectName( QStringLiteral( "trayOpenAction" ) );
    trayQuitAction = new QAction( tr( "Quit" ), this );
    trayQuitAction->setObjectName( QStringLiteral( "trayQuitAction" ) );

    trayMenu->addAction( trayOpenAction );
    trayMenu->addAction( trayQuitAction );

    connect( trayOpenAction, &QAction::triggered, this, [this] {
        if ( !applicationExitPrepared_ ) show();
    } );
    connect( trayQuitAction, &QAction::triggered, [ this ] {
        if ( applicationExitPrepared_ ) return;
        this->isCloseFromTray_ = true;
        this->close();
    } );

    trayIcon_->setIcon( mainIcon_ );
    trayIcon_->setToolTip( tr( klogg::mainwindow::trayicon::trayiconTip ).arg( productName() ) );
    trayIcon_->setContextMenu( trayMenu );

    connect( trayIcon_, &QSystemTrayIcon::activated,
             [ this ]( QSystemTrayIcon::ActivationReason reason ) {
                 if ( applicationExitPrepared_ ) return;
                 switch ( reason ) {
                 case QSystemTrayIcon::Trigger:
                     if ( !this->isVisible() ) {
                         this->show();
                     }
                     else {
                         this->hide();
                     }
                     break;
                 default:
                     break;
                 }
             } );

    if ( Configuration::get().minimizeToTray() ) {
        trayIcon_->show();
    }
}
//
// Q_SLOTS:
//

// Opens the file selection dialog to select a new log file
void MainWindow::open()
{
    QString defaultDir = ".";

    // Default to the path of the current file if there is one
    if ( auto current = currentCrawlerWidget() ) {
        QString current_file = session_.getFilename( current );
        QFileInfo fileInfo = QFileInfo( current_file );
        defaultDir = fileInfo.path();
    }

    const auto selectedFiles = QFileDialog::getOpenFileUrls(
        this, tr( "Open file" ), QUrl::fromLocalFile( defaultDir ), tr( "All files (*)" ) );

    std::vector<QUrl> localFiles;
    std::vector<QUrl> remoteFiles;

    std::partition_copy( selectedFiles.cbegin(), selectedFiles.cend(),
                         std::back_inserter( localFiles ), std::back_inserter( remoteFiles ),
                         []( const QUrl& url ) { return url.isLocalFile(); } );

    for ( const auto& localFile : localFiles ) {
        loadFile( localFile.toLocalFile() );
    }

    for ( const auto& remoteFile : remoteFiles ) {
        openRemoteFile( remoteFile );
    }
}

void MainWindow::openRemoteFile( const QUrl& url )
{
    Downloader downloader;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Downloading %1" ).arg( url.toString() ) );

    connect( &downloader, &Downloader::downloadProgress,
             [ &progressDialog ]( qint64 bytesReceived, qint64 bytesTotal ) {
                 const auto progress = calculateProgress( bytesReceived, bytesTotal );
                 progressDialog.setRange( 0, 100 );
                 progressDialog.setValue( progress );
             } );

    connect( &downloader, &Downloader::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );

    auto tempFile = new QTemporaryFile( tempDir_.filePath( url.fileName() ), this );
    if ( tempFile->open() ) {
        downloader.download( url, tempFile );
        if ( !progressDialog.exec() ) {
            loadFile( tempFile->fileName() );
        }
        else {
            QMessageBox::critical( this, tr( "%1 - File download" ).arg( productName() ),
                                   downloader.lastError() );
        }
    }
    else {
        QMessageBox::critical( this, tr( "%1 - File download" ).arg( productName() ),
                               tr( "Failed to create temp file" ) );
    }
}

void MainWindow::switchToOpenedFile( QAction* action )
{
    if ( !action ) {
        return;
    }

    loadFile( action->data().toString() );
}

void MainWindow::openFileFromRecent( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "%1 - remove from recent" ).arg( productName() ),
            tr( "Could not read file %1. Remove it from recent files?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromRecent( filename );
        }
    }
}

void MainWindow::openFileFromFavorites( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "%1 - remove from favorites" ).arg( productName() ),
            tr( "Could not read file %1. Remove it from favorites?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromFavorites( filename );
        }
    }
}

// Close current tab
void MainWindow::closeTab( ActionInitiator initiator )
{
    int currentIndex = workspace_.currentIndex();

    if ( currentIndex >= 0 ) {
        closeTab( currentIndex, initiator );
    }
    else {
        this->close();
    }
}

// Close all tabs
void MainWindow::closeAll( ActionInitiator initiator )
{
    if ( applicationExitPrepared_ && !applicationExitCommitting_ ) return;
    while ( workspace_.count() ) {
        closeTab( 0, initiator );
    }
}

// Select all the text in the currently selected view
void MainWindow::selectAll()
{
    if ( infoLine->hasFocus() ) {
        infoLine->setSelection( 0, klogg::isize( infoLine->text() ) );
    }
    else if ( auto current = currentCrawlerWidget(); current != nullptr ) {
        current->selectAll();
    }
}

// Copy the currently selected line into the clipboard
void MainWindow::copy()
{
    try {
        if ( infoLine->hasFocus() && infoLine->hasSelectedText() ) {
            sendTextToClipboard( infoLine->selectedText() );
            return;
        }

        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            auto text = current->getSelectedText();
            text.replace( QChar::Null, QChar::Space );

            sendTextToClipboard( text, true );
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Display the QuickFind bar
void MainWindow::find()
{
    displayQuickFindBar( QuickFindMux::Forward );
}

void MainWindow::clearLog()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    if ( QMessageBox::warning(
             this, tr( "%1 - clear file" ).arg( productName() ),
             tr( "Clear file %1? File content will be removed from disk, this is irreversible" )
                 .arg( current_file ) )
         == QMessageBox::Yes ) {
        QFile::resize( current_file, 0 );
    }
}

void MainWindow::copyFullPath()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    sendTextToClipboard( QDir::toNativeSeparators( current_file ) );
}

void MainWindow::openContainingFolder()
{
    showPathInFileExplorer( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::openInEditor()
{
    openFileInDefaultApplication( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::tryOpenClipboard( int tryTimes )
{
    auto clipboard = QGuiApplication::clipboard();
    auto text = clipboard->text();

    if ( text.isEmpty() && tryTimes > 0 ) {
        QTimer::singleShot( 50, [ tryTimes, this ]() { tryOpenClipboard( tryTimes - 1 ); } );
    }
    else {
        auto tempFile = new QTemporaryFile( tempDir_.filePath( "klogg_clipboard" ), this );
        if ( tempFile->open() ) {
            tempFile->write( text.toUtf8() );
            tempFile->flush();

            loadFile( tempFile->fileName() );
        }
    }
}

void MainWindow::openClipboard()
{
    tryOpenClipboard( ClipboardMaxTry );
}

void MainWindow::openUrl()
{
    bool ok;
    const auto urlInClipboard = QUrl::fromUserInput( QApplication::clipboard()->text() );
    const auto selectedUrl = urlInClipboard.isValid() ? urlInClipboard.toString() : QString{};

    QString url
        = QInputDialog::getText( this, tr( "Open URL as log file" ), tr( "URL to download:" ),
                                 QLineEdit::Normal, selectedUrl, &ok );
    if ( ok && !url.isEmpty() ) {
        openRemoteFile( url );
    }
}

// Opens the 'Highlighters' dialog box
void MainWindow::editHighlighters()
{
    HighlightersDialog dialog( this );
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    connect( &dialog, &HighlightersDialog::optionsChanged,
             [ this ]() { updateHighlightersMenu(); } );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

// Opens dialog to configure predefined filters
void MainWindow::editPredefinedFilters( const QString& newFilter )
{
    PredefinedFiltersDialog dialog( newFilter, this );

    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

// Opens the 'Options' modal dialog box
void MainWindow::options()
{
    OptionsDialog dialog( this );
    connect(&dialog, &OptionsDialog::checkUpdatesRequested, this, &MainWindow::checkUpdatesRequested);
    connect(&dialog, &OptionsDialog::optionsChanged, this, &MainWindow::updatePreferencesChanged);
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
    connect( &dialog, &OptionsDialog::restartRequested, this, [ this ] {
        QTimer::singleShot( 0, this, [ this ] { Q_EMIT restartRequested(); } );
    } );

    connect( &dialog, &OptionsDialog::optionsChanged, [ this ]() {
        const auto& config = Configuration::get();
        logging::enableFileLogging( config.enableLogging(),
                                    static_cast<logging::LogLevel>( config.loggingLevel() ) );

        newWindowAction->setVisible( config.allowMultipleWindows() );
        followAction->setEnabled( config.anyFileWatchEnabled() );

        updateShortcuts();
        updateRecentFileActions();
        Q_EMIT uiThemeChanged( config.uiThemeMode() );
    } );
    dialog.exec();

    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

void MainWindow::about()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::aboutQt()
{
    QMessageBox::aboutQt( this, tr( "About Qt" ) );
}

void MainWindow::documentation()
{
    auto* browser = findChild<QTextBrowser*>(QStringLiteral("documentationWindow"),
                                             Qt::FindDirectChildrenOnly);
    if (!browser) browser = new DocumentationWindow(this);
    browser->showNormal();
    browser->raise();
    browser->activateWindow();
}

void MainWindow::showScratchPad()
{
    auto state = scratchPad_.windowState();
    state.setFlag( Qt::WindowMinimized, false );
    scratchPad_.setWindowState( state );

    scratchPad_.show();
    scratchPad_.activateWindow();
}

void MainWindow::sendToScratchpad( QString newData )
{
    scratchPad_.addData( newData );
    showScratchPad();
}

void MainWindow::replaceDataInScratchpad( QString newData )
{
    scratchPad_.replaceData( newData );
    showScratchPad();
}

void MainWindow::encodingChanged( QAction* action )
{
    const auto mibData = action->data();
    std::optional<int> mib;
    if ( mibData.isValid() ) {
        mib = mibData.toInt();
    }

    LOG_DEBUG << "encodingChanged, encoding " << mib.value_or( 0 );
    if ( auto crawler = currentCrawlerWidget() ) {
        crawler->setEncoding( mib );
        updateInfoLine();
    }
}

void MainWindow::toggleOverviewVisibility( bool isVisible )
{
    auto& config = Configuration::get();
    config.setOverviewVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::changeFollowMode( bool follow )
{
    auto& config = Configuration::get();
    if ( follow && !( config.nativeFileWatchEnabled() || config.pollingEnabled() ) ) {
        LOG_WARNING << "File watch disabled in settings";
    }

    followAction->setChecked( follow );
}

void MainWindow::lineNumberHandler( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                                    LineLength nSymbols )
{
    selectedStartLine_ = startLine;
    selectedLineCount_ = nLines;
    selectedStartColumn_ = startCol;
    selectedSymbolCount_ = nSymbols;
    renderLineNumberStatus();
}

void MainWindow::renderLineNumberStatus()
{
    // The line number received is the internal (starts at 0)
    uint64_t fileSize{};
    uint64_t fileNbLine{};
    QDateTime lastModified;

    auto* const crawler = currentCrawlerWidget();
    if ( crawler == nullptr ) {
        lineNbField->clear();
        return;
    }
    session_.getFileInfo( crawler, &fileSize, &fileNbLine, &lastModified );

    if ( fileNbLine != 0 ) {
        if ( selectedSymbolCount_.get() == 0 ) {
            lineNbField->setText(
                tr( "Ln:%1/%2" ).arg( selectedStartLine_.get() + 1 ).arg( fileNbLine ) );
        }
        else {
            if ( selectedLineCount_.get() == 1 ) {
                // portion selection on one line
                lineNbField->setText( tr( "Ln:%1/%2 Col:%3 Sel:%4|%5" )
                                          .arg( selectedStartLine_.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( selectedStartColumn_.get() )
                                          .arg( selectedSymbolCount_.get() )
                                          .arg( selectedLineCount_.get() ) );
            }
            else {
                // multiple lines selection
                lineNbField->setText( tr( "Ln:%1/%2 Sel:%4|%5" )
                                          .arg( selectedStartLine_.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( selectedSymbolCount_.get() )
                                          .arg( selectedLineCount_.get() ) );
            }
        }
    }
    else {
        lineNbField->clear();
    }
}

void MainWindow::newPredefinedFilterHandler( QString newFilter )
{
    editPredefinedFilters( newFilter );
}

void MainWindow::updateLoadingProgress( int progress )
{
    LOG_DEBUG << "Loading progress: " << progress;

    // Zero is still a loading state owned by the current document. Completion
    // is reported separately through loadingFinished().
    if ( progress >= 0 && progress < 100 ) {
        infoDisplayState_ = InfoDisplayState::Loading;
        loadingProgress_ = progress;
        updateInfoLine();

        stopAction->setEnabled( true );
        reloadAction->setEnabled( false );
    }
}

void MainWindow::handleLoadingFinished( LoadingStatus status )
{
    LOG_DEBUG << "handleLoadingFinished success=" << ( status == LoadingStatus::Successful );

    // No file is loading
    loadingFileName.clear();
    infoDisplayState_ = InfoDisplayState::Normal;

    if ( status == LoadingStatus::Successful ) {
        updateInfoLine();

        infoLine->hideGauge();
        showInfoLabels( true );
        stopAction->setEnabled( false );
        reloadAction->setEnabled( true );

        lineNumberHandler( 0_lnum, LinesCount( 0 ), LineColumn( 0 ), LineLength( 0 ) );

        // Now everything is ready, we can finally show the file!
        currentCrawlerWidget()->show();
    }
    else {
        if ( status == LoadingStatus::NoMemory ) {
            QMessageBox alertBox;
            alertBox.setText( tr( "Not enough memory." ) );
            alertBox.setInformativeText(
                tr( "The system does not have enough memory to hold the index for this file. The "
                    "file will now be closed." ) );
            alertBox.setIcon( QMessageBox::Critical );
            alertBox.exec();
        }

        closeTab( workspace_.currentIndex(), ActionInitiator::App );
    }

    // workspace_.setEnabled( true );
}

void MainWindow::handleFilteredViewChanged()
{
    workspace_.refreshQuickFindSelector();
}

void MainWindow::closeTab( int index, ActionInitiator initiator )
{
    if ( applicationExitPrepared_ && !applicationExitCommitting_ ) return;
    const QString path = workspace_.closeDocument( index );
    if ( !path.isEmpty() && initiator == ActionInitiator::User )
        addRecentFile( path );
    updateOpenedFilesMenu();
}

void MainWindow::currentDocumentChanged( CrawlerWidget* crawler_widget )
{
    LOG_DEBUG << "currentDocumentChanged";

    if ( crawler_widget ) {
        // New tab is set up with fonts etc...
        Q_EMIT optionsChanged();

        updateMenuBarFromDocument( crawler_widget );
        updateTitleBar( session_.getFilename( crawler_widget ) );
        updateFavoritesMenu();

        editMenu->setEnabled( true );
    }
    else {
        // No tab left
        infoLine->hideGauge();
        infoLine->clear();
        showInfoLabels( false );

        updateTitleBar( QString() );

        editMenu->setEnabled( false );
        addToFavoritesAction->setEnabled( false );
        addToFavoritesMenuAction->setEnabled( false );
    }
}

void MainWindow::changeQFPattern( const QString& newPattern )
{
    quickFindWidget_.changeDisplayedPattern( newPattern, true );
}

void MainWindow::loadFileNonInteractive( const QString& file_name )
{
    if ( applicationExitPrepared_ ) {
        LOG_WARNING << "File request rejected while application exit is prepared: " << file_name;
        return;
    }
    LOG_DEBUG << "loadFileNonInteractive( " << file_name.toStdString() << " )";

    loadFile( file_name );

    // Try to get the window to the front
    // This is a bit of a hack but has been tested on:
    // Qt 5.3 / Gnome / Linux
    // Qt 5.11 / Win10
#ifdef Q_OS_WIN
    const auto isMaximized = isMaximized_;

    if ( isMaximized ) {
        showMaximized();
    }
    else {
        showNormal();
    }

    activateWindow();
    raise();
#else
    Qt::WindowFlags window_flags = windowFlags();
    window_flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );

    raise();
    activateWindow();

    window_flags = windowFlags();
    window_flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );
    show();
#endif

    if ( auto currentCrawler = currentCrawlerWidget() ) {
        currentCrawler->setFocus();
    }
}

//
// Events
//

bool MainWindow::prepareForApplicationExit()
{
    if ( applicationExitPrepared_ || property( "zzlogg.test.rejectApplicationClose" ).toBool() ) {
        return false;
    }
    applicationExitPrepared_ = true;
    enabledBeforeExitPreparation_ = isEnabled();
    actionsBeforeExitPreparation_.clear();
    for ( auto* action : findChildren<QAction*>() ) {
        actionsBeforeExitPreparation_.append( { action, action->isEnabled() } );
        action->setEnabled( false );
    }
    setEnabled( false );
    writeSettings();
    return true;
}

void MainWindow::cancelApplicationExitPreparation()
{
    if ( !applicationExitPrepared_ ) return;
    applicationExitPrepared_ = false;
    setEnabled( enabledBeforeExitPreparation_ );
    for ( const auto& entry : actionsBeforeExitPreparation_ ) {
        if ( entry.first ) entry.first->setEnabled( entry.second );
    }
    actionsBeforeExitPreparation_.clear();
}

bool MainWindow::canCommitApplicationExit() const
{
    return applicationExitPrepared_
           && !property( "zzlogg.test.rejectApplicationClose" ).toBool();
}

bool MainWindow::commitPreparedApplicationExit()
{
    if ( !canCommitApplicationExit() ) return false;
    applicationExitCommitting_ = true;
    const bool closed = closeForApplicationExit();
    applicationExitCommitting_ = false;
    return closed;
}

bool MainWindow::closeForApplicationExit()
{
    if ( applicationExitPrepared_ && !applicationExitCommitting_ ) return false;
    const bool previousCloseFromTray = isCloseFromTray_;
    isCloseFromTray_ = true;
    const bool closed = close();
    if ( !closed ) {
        isCloseFromTray_ = previousCloseFromTray;
    }
    return closed;
}

// Closes the application
void MainWindow::closeEvent( QCloseEvent* event )
{
    if ( applicationExitPrepared_ && !applicationExitCommitting_ ) {
        event->ignore();
        return;
    }
    if ( !isCloseFromTray_ && this->isVisible() && Configuration::get().minimizeToTray() ) {
        event->ignore();
        trayIcon_->show();
        this->hide();
    }
    else {
        const auto saveSettings = session_.close();
        if ( saveSettings && !applicationExitPrepared_ ) {
            writeSettings();
        }
        applicationExitPrepared_ = false;

        closeAll( ActionInitiator::App );
        trayIcon_->hide();
        Q_EMIT windowClosed();

        event->accept();
    }
}

// Minimize handling the application
void MainWindow::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::WindowStateChange ) {
        isMaximized_ = windowState().testFlag( Qt::WindowMaximized );

        if ( this->windowState() & Qt::WindowMinimized ) {
            if ( Configuration::get().minimizeToTray() ) {
                dispatchToMainThread( [ this ] {
                    trayIcon_->show();
                    this->hide();
                } );
            }
        }
    }
    else if ( event->type() == QEvent::StyleChange ) {
        dispatchToObject( [ this ] {
            loadIcons();
            updateOpenedFilesMenu();
            updateFavoritesMenu();
            updateHighlightersMenu();
        }, this );
    }
    else if ( event->type() == QEvent::PaletteChange
              || event->type() == QEvent::ApplicationPaletteChange ) {
        dispatchToObject( [ this ] { loadIcons(); }, this );
    }
    else if ( event->type() == QEvent::LanguageChange ) {
        reTranslateUI();
    }

    QMainWindow::changeEvent( event );
}

// Accepts the drag event if it looks like a filename
void MainWindow::dragEnterEvent( QDragEnterEvent* event )
{
    if ( applicationExitPrepared_ ) { event->ignore(); return; }
    if ( event->mimeData()->hasFormat( "text/uri-list" ) )
        event->acceptProposedAction();
}

// Tries and loads the file if the URL dropped is local
void MainWindow::dropEvent( QDropEvent* event )
{
    if ( applicationExitPrepared_ ) { event->ignore(); return; }
    const QList<QUrl> urls = event->mimeData()->urls();

    for ( const auto& url : urls ) {
        auto fileName = url.toLocalFile();
        if ( fileName.isEmpty() )
            continue;

        loadFile( fileName );
    }
}

bool MainWindow::event( QEvent* event )
{
    if ( event->type() == QEvent::WindowActivate ) {
        Q_EMIT windowActivated();
    }
    else if ( event->type() == QEvent::Show ) {
        if ( this->windowHandle() ) {
            std::call_once( screenChangesConnect_, [ this ]() {
                logScreenInfo( this->windowHandle()->screen() );
                connect( this->windowHandle(), &QWindow::screenChanged,
                         [ this ]( QScreen* screen ) { logScreenInfo( screen ); } );
            } );
        }
    }

    return QMainWindow::event( event );
}

//
// Private functions
//

bool MainWindow::extractAndLoadFile( const QString& fileName )
{
    const auto& config = Configuration::get();

    if ( !config.extractArchives() ) {
        return false;
    }

    if ( !config.extractArchivesAlways() ) {
        const auto userChoice
            = QMessageBox::question( this, productName(), tr( "Extract archive to temp folder?" ) );
        if ( userChoice == QMessageBox::No ) {
            return false;
        }
    }

    const auto decompressAction = Decompressor::action( fileName );

    Decompressor decompressor;
    AtomicFlag decompressInterrupt;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Extracting %1" ).arg( fileName ) );
    progressDialog.setRange( 0, 0 );

    connect( &decompressor, &Decompressor::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );
    connect( &progressDialog, &QProgressDialog::canceled,
             [ &decompressInterrupt, &decompressor ]() {
                 decompressInterrupt.set();
                 decompressor.waitForResult();
             } );

    if ( decompressAction == DecompressAction::Decompress ) {

        auto tempFile = new QTemporaryFile(
            this->tempDir_.filePath( QFileInfo( fileName ).fileName() ), this );

        if ( tempFile->open() && decompressor.decompress( fileName, tempFile, decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            return this->loadFile( tempFile->fileName() );
        }
        else {
            QMessageBox::warning(
                this, productName(),
                tr( "Failed to decompress %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }
    else if ( decompressAction == DecompressAction::Extract ) {
        QTemporaryDir archiveDir{ this->tempDir_.filePath( QFileInfo( fileName ).fileName() ) };
        archiveDir.setAutoRemove( false );
        if ( decompressor.extract( fileName, archiveDir.path(), decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            const auto selectedFiles = QFileDialog::getOpenFileNames(
                this, tr( "Open file from archive" ), archiveDir.path(), tr( "All files (*)" ) );

            for ( const auto& extractedFile : selectedFiles ) {
                this->loadFile( extractedFile );
            }

            return true;
        }
        else {
            QMessageBox::warning(
                this, productName(),
                tr( "Failed to extract %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }

    return false;
}

// Create a CrawlerWidget for the passed file, start its loading
// and update the title bar.
// The loading is done asynchronously.
bool MainWindow::loadFile( const QString& fileName, bool followFile )
{
    if ( applicationExitPrepared_ ) {
        LOG_WARNING << "File request rejected while application exit is prepared: " << fileName;
        return false;
    }
    LOG_DEBUG << "loadFile ( " << fileName.toStdString() << " )";

    // First check if the file is already open...
    auto* existing_crawler = static_cast<CrawlerWidget*>( session_.getViewIfOpen( fileName ) );

    if ( existing_crawler ) {
        auto* crawlerWindow = qobject_cast<MainWindow*>( existing_crawler->window() );
        crawlerWindow->workspace_.setCurrentWidget( existing_crawler );
        crawlerWindow->activateWindow();
        return true;
    }

    const auto decompressAction = Decompressor::action( fileName );

    if ( decompressAction == DecompressAction::None || !Configuration::get().extractArchives() ) {
        // Load the file
        loadingFileName = fileName;

        try {
            const auto previousViewContext = [ &fileName ]() {
                const auto& session = SessionInfo::getSynced();
                const auto& windows = session.windows();
                for ( const auto& windowId : windows ) {
                    const auto openedFiles = session.openFiles( windowId );
                    const auto existingContext
                        = std::find_if( openedFiles.begin(), openedFiles.end(),
                                        [ &fileName ]( const auto& context ) {
                                            return context.fileName == fileName;
                                        } );
                    if ( existingContext != openedFiles.end() ) {
                        return existingContext->viewContext;
                    }
                }
                return QString{};
            }();

            auto* crawler_widget = workspace_.openDocument( fileName, previousViewContext );

            if ( !crawler_widget ) {
                LOG_ERROR << "Can't create crawler for " << fileName.toStdString();
                return false;
            }

            addRecentFile( fileName );
            updateOpenedFilesMenu();

            const auto& config = Configuration::get();
            if ( config.anyFileWatchEnabled() && ( followFile || config.followFileOnLoad() ) ) {
                signalCrawlerToFollowFile( crawler_widget );
                followAction->setChecked( true );
            }
        } catch ( ... ) {
            LOG_ERROR << "Can't open file " << fileName.toStdString();
            return false;
        }

        LOG_DEBUG << "Success loading file " << fileName.toStdString();
        return true;
    }
    else {
        return extractAndLoadFile( fileName );
    }
}

// Strips the passed filename from its directory part.
QString MainWindow::strippedName( const QString& fullFileName ) const
{
    return QFileInfo( fullFileName ).fileName();
}

// Return the currently active CrawlerWidget, or NULL if none
CrawlerWidget* MainWindow::currentCrawlerWidget() const
{
    return workspace_.currentDocument();
}

// Update the title bar.
void MainWindow::updateTitleBar( const QString& file_name )
{
    QString shownName = tr( "Untitled" );
    if ( !file_name.isEmpty() ) {
        shownName = strippedName( file_name );
    }

    QString indexPart = "";
    if ( session_.windowIndex() > 0 ) {
        indexPart = QString( " #%1" ).arg( session_.windowIndex() + 1 );
    }

    setWindowTitle( tr( "%1 - %2%3" ).arg( shownName, productName(), indexPart ) );
    Q_EMIT activeDocumentNameChanged( file_name.isEmpty() ? QString{} : strippedName( file_name ) );
}

void MainWindow::addRecentFile( const QString& fileName )
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.addRecent( fileName );
    recentFiles.save();
    updateRecentFileActions();
}

// Updates the actions for the recent files.
// Must be called after having added a new name to the list.
void MainWindow::updateRecentFileActions()
{
    auto& recentFiles = RecentFiles::get();
    QStringList recent_files = recentFiles.recentFiles();
    int recent_files_max_items = recentFiles.getNumberItemsToShow();

    if ( recentFiles.recentFiles().count() > 0 ) {
        recentFilesMenu->setEnabled( true );
        for ( auto j = 0; j < MAX_RECENT_FILES; ++j ) {
            const auto actionIndex = static_cast<size_t>( j );
            if ( j < recent_files_max_items ) {
                int key = j + ( ( j < 9 ) ? 0x31 : ( 0x61 - 9 ) ); // shortcuts: 1..9 next a,b...
                QString text
                    = tr( "&%1 %2" ).arg( QChar( key ) ).arg( strippedName( recent_files[ j ] ) );
                recentFileActions[ actionIndex ]->setText( text );
                recentFileActions[ actionIndex ]->setToolTip( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setData( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setVisible( true );
            }
            else {
                recentFileActions[ actionIndex ]->setVisible( false );
            }
        }
    }
    else {
        recentFilesMenu->setEnabled( false );
    }

    // separatorAction->setVisible(!recentFiles.isEmpty());
}

// Clear the list of the recent files
void MainWindow::clearRecentFileActions()
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.removeAll();
    recentFiles.save();
    updateRecentFileActions();
}
// Update our menu bar to match the settings of the crawler
// (used when the tab is changed)
void MainWindow::updateMenuBarFromDocument( const CrawlerWidget* crawler )
{
    const auto encodingMib = crawler->encodingMib();

    auto encodingActions = encodingGroup->actions();
    auto encodingItem = std::find_if( encodingActions.begin(), encodingActions.end(),
                                      [ &encodingMib ]( const auto& action ) {
                                          return ( !encodingMib && !action->data().isValid() )
                                                 || ( encodingMib && action->data().isValid()
                                                      && *encodingMib == action->data().toInt() );
                                      } );

    if ( encodingItem != encodingActions.end() ) {
        ( *encodingItem )->setChecked( true );
    }

    followAction->setChecked( crawler->isFollowEnabled() );
    textWrapAction->setChecked( crawler->isTextWrapEnabled() );
}

// Update the top info line from the session
void MainWindow::updateInfoLine()
{
    QLocale defaultLocale;
    auto* const crawler = currentCrawlerWidget();
    Q_ASSERT( crawler != nullptr );

    // Following should always work as we will only receive enter
    // this slot if there is a crawler connected.
    QString current_file
        = QDir::toNativeSeparators( session_.getFilename( crawler ) );

    infoLine->setPath( current_file );
    encodingField->setText( crawler->encodingText() );
    if ( infoDisplayState_ == InfoDisplayState::Loading ) {
        infoLine->setText(
            current_file + tr( " - Indexing lines... (%1 %)" ).arg( loadingProgress_ ) );
        infoLine->displayGauge( loadingProgress_ );
        showInfoLabels( false );
        return;
    }

    uint64_t fileSize;
    uint64_t fileNbLine;
    QDateTime lastModified;

    session_.getFileInfo( crawler, &fileSize, &fileNbLine, &lastModified );

    infoLine->setText( current_file );
    infoLine->hideGauge();
    showInfoLabels( true );
    sizeField->setText( readableSize( fileSize ) );

    if ( lastModified.isValid() ) {
        const QString date = defaultLocale.toString( lastModified, QLocale::NarrowFormat );
        dateField->setText( tr( "modified on %1" ).arg( date ) );
        dateField->show();
    }
    else {
        dateField->hide();
    }
}

void MainWindow::retranslateStatusUi()
{
    if ( currentCrawlerWidget() != nullptr ) {
        updateInfoLine();
        renderLineNumberStatus();
    }
}

void MainWindow::updateOpenedFilesMenu()
{
    openedFilesMenu->clear();

    const auto& files = session_.openedFiles();

    openedFilesMenu->setEnabled( !files.empty() );

    openedFilesMenu->addAction( selectOpenFileAction );
    openedFilesMenu->addSeparator();

    for ( const auto& file : files ) {
        const auto displayFile = DisplayFilePath{ file };
        auto action = openedFilesMenu->addAction( displayFile.displayName() );

        action->setActionGroup( openedFilesGroup );
        action->setToolTip( displayFile.nativeFullPath() );
        action->setData( displayFile.fullPath() );
    }

    selectOpenFileAction->setDisabled( files.empty() );
}

void MainWindow::updateHighlightersMenu()
{
    highlightersMenu->clearHighlightersMenu();
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->addAction( editHighlightersAction, true );
    highlightersMenu->populateHighlightersMenu();
}

void MainWindow::updateFavoritesMenu()
{
    favoritesMenu->clear();

    favoritesMenu->addAction( addToFavoritesMenuAction );
    favoritesMenu->addAction( removeFromFavoritesAction );

    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );

    using namespace klogg::mainwindow;

    addToFavoritesAction->setText(
        QApplication::translate( "klogg::mainwindow::action", action::addToFavoritesText ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesAction->setData( true );

    const auto& favorites = FavoriteFiles::getSynced().favorites();
    auto crawler = currentCrawlerWidget();

    addToFavoritesAction->setEnabled( crawler != nullptr );
    addToFavoritesMenuAction->setEnabled( crawler != nullptr );
    removeFromFavoritesAction->setEnabled( !favorites.empty() );

    if ( crawler ) {
        const auto path = session_.getFilename( crawler );
        if ( std::any_of( favorites.begin(), favorites.end(), FullPathComparator( path ) ) ) {

            addToFavoritesAction->setText( QApplication::translate(
                "klogg::mainwindow::action", action::removeFromFavoritesText ) );
            addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
            addToFavoritesAction->setData( false );

            addToFavoritesMenuAction->setEnabled( false );
            addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
        }
    }

    favoritesMenu->addSeparator();

    for ( const auto& file : favorites ) {
        auto action = favoritesMenu->addAction( file.displayName() );

        action->setActionGroup( favoritesGroup );
        action->setToolTip( file.nativeFullPath() );
        action->setData( file.fullPath() );
    }
}

void MainWindow::addToFavorites()
{
    if ( const auto crawler = currentCrawlerWidget() ) {
        auto& favorites = FavoriteFiles::get();
        const auto path = session_.getFilename( crawler );

        if ( addToFavoritesAction->data().toBool() ) {
            favorites.add( path );
        }
        else {
            favorites.remove( path );
        }

        favorites.save();

        updateFavoritesMenu();
    }
}

void MainWindow::removeFromFavorites()
{
    const auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    QStringList files;
    std::transform( favorites.cbegin(), favorites.cend(), std::back_inserter( files ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto currentIndex = 0;

    if ( const auto crawler = currentCrawlerWidget() ) {
        const auto currentPath = session_.getFilename( crawler );
        const auto currentItem
            = std::find_if( favorites.begin(), favorites.end(), FullPathComparator( currentPath ) );
        if ( currentItem != favorites.end() ) {
            currentIndex = static_cast<int>( std::distance( favorites.begin(), currentItem ) );
        }
    }

    bool ok = false;
    const auto pathToRemove = QInputDialog::getItem( this, tr( "Remove from favorites" ),
                                                     tr( "Select item to remove from favorites" ),
                                                     files, currentIndex, false, &ok );
    if ( ok ) {
        removeFromFavorites( pathToRemove );
    }
}

void MainWindow::removeFromFavorites( const QString& pathToRemove )
{
    auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    const auto selectedFile = std::find_if( favorites.begin(), favorites.end(),
                                            [ pathToRemove ]( const DisplayFilePath& f ) {
                                                return f.nativeFullPath() == pathToRemove;
                                            } );

    if ( selectedFile != favorites.end() ) {
        favoriteFiles.remove( selectedFile->fullPath() );
        favoriteFiles.save();
        updateFavoritesMenu();
    }
}

void MainWindow::removeFromRecent( const QString& pathToRemove )
{
    auto& recentFiles = RecentFiles::get();
    recentFiles.removeRecent( pathToRemove );
    recentFiles.save();
    updateRecentFileActions();
}

void MainWindow::selectOpenedFile()
{
    auto openedFilesPaths = session_.openedFiles();
    std::vector<DisplayFilePath> openedFiles;
    openedFiles.reserve( openedFilesPaths.size() );
    std::transform( openedFilesPaths.cbegin(), openedFilesPaths.cend(),
                    std::back_inserter( openedFiles ),
                    []( const auto& path ) { return DisplayFilePath{ path }; } );

    QStringList filesToShow;
    std::transform( openedFiles.cbegin(), openedFiles.cend(), std::back_inserter( filesToShow ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto selectFileDialog = std::make_unique<QDialog>( this );
    selectFileDialog->setWindowTitle( tr( "%1 -- switch to file" ).arg( productName() ) );
    selectFileDialog->setMinimumWidth( 800 );
    selectFileDialog->setMinimumHeight( 600 );

    auto filesModel = std::make_unique<QStringListModel>( filesToShow, selectFileDialog.get() );
    auto filteredModel = std::make_unique<QSortFilterProxyModel>( selectFileDialog.get() );
    filteredModel->setSourceModel( filesModel.get() );

    auto filesView = std::make_unique<QListView>();
    filesView->setModel( filteredModel.get() );
    filesView->setEditTriggers( QAbstractItemView::NoEditTriggers );
    filesView->setSelectionMode( QAbstractItemView::SingleSelection );

    auto filterEdit = std::make_unique<QLineEdit>();
    auto buttonBox
        = std::make_unique<QDialogButtonBox>( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    connect( buttonBox.get(), &QDialogButtonBox::accepted, selectFileDialog.get(),
             &QDialog::accept );
    connect( buttonBox.get(), &QDialogButtonBox::rejected, selectFileDialog.get(),
             &QDialog::reject );

    connect( filterEdit.get(), &QLineEdit::textEdited,
             [ model = filteredModel.get(), view = filesView.get() ]( const QString& filter ) {
                 model->setFilterWildcard( filter );
                 model->invalidate();
                 view->selectionModel()->select( model->index( 0, 0 ),
                                                 QItemSelectionModel::SelectCurrent );
             } );

    dispatchToMainThread( [ edit = filterEdit.get() ]() { edit->setFocus(); } );

    connect( selectFileDialog.get(), &QDialog::finished,
             [ this, openedFiles, dialog = selectFileDialog.get(), model = filteredModel.get(),
               view = filesView.get() ]( auto result ) {
                 dialog->deleteLater();
                 if ( result != QDialog::Accepted || !view->selectionModel()->hasSelection() ) {
                     return;
                 }
                 const auto& selectedPath
                     = model->data( view->selectionModel()->selectedIndexes().front() ).toString();
                 const auto selectedFile
                     = std::find_if( openedFiles.begin(), openedFiles.end(),
                                     [ selectedPath ]( const DisplayFilePath& f ) {
                                         return f.nativeFullPath() == selectedPath;
                                     } );

                 if ( selectedFile != openedFiles.end() ) {
                     loadFile( selectedFile->fullPath() );
                 }
             } );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( filesView.release() );
    layout->addWidget( filterEdit.release() );
    layout->addWidget( buttonBox.release() );

    selectFileDialog->setLayout( layout.release() );
    selectFileDialog->setModal( true );
    selectFileDialog->open();

    filesModel.release();
    filteredModel.release();
    selectFileDialog.release();
}

void MainWindow::showInfoLabels( bool show )
{
    for ( auto separator : infoToolbarSeparators ) {
        separator->setVisible( show );
    }
    if ( !show ) {
        sizeField->clear();
        dateField->clear();
        encodingField->clear();
        lineNbField->clear();
    }
}

// Write settings to permanent storage
void MainWindow::writeSettings()
{
    workspace_.saveDocuments( saveGeometry() );
}

// Read settings from permanent storage
void MainWindow::readSettings()
{
    // Get and restore the session
    // auto& session = SessionInfo::getSynced();
    /*
     * FIXME: should be in the session
    crawlerWidget->restoreState( session.crawlerState() );
    */

    // History of recent files
    RecentFiles::getSynced();
    updateRecentFileActions();

    FavoriteFiles::getSynced();
    updateFavoritesMenu();

    HighlighterSetCollection::getSynced();
    updateHighlightersMenu();
}

void MainWindow::displayQuickFindBar( QuickFindMux::QFDirection direction )
{
    LOG_DEBUG << "MainWindow::displayQuickFindBar";

    // Warn crawlers so they can save the position of the focus in order
    // to do incremental search in the right view.
    Q_EMIT enteringQuickFind();

    const auto crawler = currentCrawlerWidget();
    if ( crawler != nullptr && crawler->isPartialSelection() ) {
        auto selection = crawler->getSelectedText();
        if ( !selection.isEmpty() ) {
            quickFindWidget_.changeDisplayedPattern( selection, false );
        }
    }

    quickFindMux_.setDirection( direction );
    quickFindWidget_.userActivate();
}

void MainWindow::logScreenInfo( QScreen* screen )
{
    LOG_INFO << "screen changed for " << session_.windowIndex();
    if ( screen == nullptr ) {
        return;
    }

    LOG_INFO << "screen name " << screen->name();
    LOG_INFO << "screen size " << screen->size().width() << "x" << screen->size().height();
    LOG_INFO << "screen ratio " << screen->devicePixelRatio();
    LOG_INFO << "screen logical dpi " << screen->logicalDotsPerInch();
    LOG_INFO << "screen physical dpi " << screen->physicalDotsPerInch();
}
