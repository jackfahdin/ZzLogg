/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
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

// This file implements the CrawlerWidget class.
// It is responsible for creating and managing the two views and all
// the UI elements.  It implements the connection between the UI elements.
// It also interacts with the sets of data (full and filtered).

#include "abstractlogview.h"
#include "active_screen.h"
#include "linetypes.h"
#include "log.h"

#include <algorithm>
#include <cassert>
#include <chrono>

#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QShortcut>
#include <QStandardItemModel>
#include <QStringListModel>
#include <qglobal.h>
#include <qobject.h>
#include <string>

#include "regularexpression.h"

#include "crawlerwidget.h"

#include "configuration.h"
#include "dispatch_to.h"
#include "fontutils.h"
#include "infoline.h"
#include "quickfindpattern.h"
#include "savedsearches.h"
#include "shortcuts.h"

static constexpr char AnsiColorSequenceRegex[] = "\\x1B\\[([0-9]{1,4}((;|:)[0-9]{1,3})*)?[mK]";
static constexpr char SearchPatternProperty[] = "zzlogg.searchPattern";

// Palette for error signaling (yellow background)
const QPalette CrawlerWidget::ErrorPalette( Qt::darkYellow );

// Implementation of the view context for the CrawlerWidget
class CrawlerWidgetContext : public ViewContextInterface {
public:
    // Construct from the stored string representation
    explicit CrawlerWidgetContext( const QString& string );
    // Construct from the value passsed
    CrawlerWidgetContext( QList<int> sizes, bool ignoreCase, bool autoRefresh, bool followFile,
                          bool useRegexp, bool inverseRegexp, bool useBooleanCombination,
                          QList<LineNumber> markedLines )
        : sizes_( sizes )
        , ignoreCase_( ignoreCase )
        , autoRefresh_( autoRefresh )
        , followFile_( followFile )
        , useRegexp_( useRegexp )
        , inverseRegexp_( inverseRegexp )
        , useBooleanCombination_( useBooleanCombination )
    {
        std::transform( markedLines.cbegin(), markedLines.cend(), std::back_inserter( marks_ ),
                        []( const auto& m ) { return m.get(); } );
    }

    // Implementation of the ViewContextInterface function
    QString toString() const override;

    // Access the Qt sizes array for the QSplitter
    QList<int> sizes() const
    {
        return sizes_;
    }

    bool ignoreCase() const
    {
        return ignoreCase_;
    }
    bool autoRefresh() const
    {
        return autoRefresh_;
    }
    bool followFile() const
    {
        return followFile_;
    }
    bool useRegexp() const
    {
        return useRegexp_;
    }
    bool inverseRegexp() const
    {
        return inverseRegexp_;
    }
    bool useBooleanCombination() const
    {
        return useBooleanCombination_;
    }

    QList<LineNumber::UnderlyingType> marks() const
    {
        return marks_;
    }

private:
    void loadFromString( const QString& string );
    void loadFromJson( const QString& json );

private:
    QList<int> sizes_;

    bool ignoreCase_;
    bool autoRefresh_;
    bool followFile_;
    bool useRegexp_;
    bool inverseRegexp_;
    bool useBooleanCombination_;

    QList<LineNumber::UnderlyingType> marks_;
};

// Constructor only does trivial construction. The real work is done once
// the data is attached.
CrawlerWidget::CrawlerWidget( QWidget* parent )
    : LogPage( parent )
    , iconLoader_{ this }
{
}

// The top line is first one on the main display
LineNumber CrawlerWidget::getTopLine() const
{
    return logMainView_->getTopLine();
}

QString CrawlerWidget::getSelectedText() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->getSelectedText();
    else
        return logMainView_->getSelectedText();
}

bool CrawlerWidget::isPartialSelection() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->isPartialSelection();
    else
        return logMainView_->isPartialSelection();
}

void CrawlerWidget::selectAll()
{
    activeView()->selectAll();
}

std::optional<int> CrawlerWidget::encodingMib() const
{
    return encodingMib_;
}

bool CrawlerWidget::isFollowEnabled() const
{
    return logMainView_->isFollowEnabled();
}

bool CrawlerWidget::isTextWrapEnabled() const
{
    return logMainView_->isTextWrapEnabled();
}

void CrawlerWidget::reloadPredefinedFilters() const
{
    searchPanel_->predefinedFilters()->populatePredefinedFilters();
}

QString CrawlerWidget::encodingText() const
{
    return encodingText_;
}

// Return a pointer to the view in which we should do the QuickFind
SearchableWidgetInterface* CrawlerWidget::doGetActiveSearchable() const
{
    return activeView();
}

// Return all the searchable widgets (views)
std::vector<QObject*> CrawlerWidget::doGetAllSearchables() const
{
    std::vector<QObject*> searchables = { logMainView_, filteredView_ };

    return searchables;
}

// Update the state of the parent
void CrawlerWidget::doSendAllStateSignals()
{
    Q_EMIT newSelection( currentLineNumber_, 0_lcount, 0_lcol, 0_length );
    if ( loadingInProgress_ )
        Q_EMIT loadingProgressed( loadingProgress_ );
    else
        Q_EMIT loadingFinished( LoadingStatus::Successful );
}

void CrawlerWidget::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );

    if ( event->type() == QEvent::LanguageChange && searchPanel_ != nullptr ) {
        retranslateUi();
        Q_EMIT languageDisplayChanged();
    }

    const bool themeVisualChanged = event->type() == QEvent::StyleChange
                                    || event->type() == QEvent::PaletteChange
                                    || event->type() == QEvent::ApplicationPaletteChange;
    if ( themeVisualChanged && searchPanel_ != nullptr ) {
        if ( searchPanel_->searchInfoLine() != nullptr ) {
            searchPanel_->searchInfoLine()->refreshGaugePalette( palette() );
        }
        dispatchToObject( [ this ] { loadIcons(); }, this );
    }
}

//
// Public Q_SLOTS:
//

void CrawlerWidget::stopLoading()
{
    logFilteredData_->interruptSearch();
    logData_->interruptLoading();
}

void CrawlerWidget::reload()
{
    searchState_.resetState();
    constexpr auto DropCache = true;
    logFilteredData_->clearSearch( DropCache );
    logFilteredData_->clearMarks();
    filteredView_->updateData();
    printSearchInfoMessage();

    logData_->reload();

    // A reload is considered as a first load,
    // this is to prevent the "new data" icon to be triggered.
    firstLoadDone_ = false;
}

void CrawlerWidget::setEncoding( std::optional<int> mib )
{
    encodingMib_ = std::move( mib );
    updateEncoding();

    update();
}

void CrawlerWidget::focusSearchEdit()
{
    searchPanel_->searchLineEdit()->setFocus( Qt::ShortcutFocusReason );
}

void CrawlerWidget::goToLine()
{
    bool isLineSelected = true;
    auto newLine = QInputDialog::getText( this, "Jump to line", "Line number" )
                       .toULongLong( &isLineSelected );

    if ( isLineSelected ) {
        if ( newLine == 0 ) {
            newLine = 1;
        }

        const auto selectedLine
            = LineNumber( static_cast<LineNumber::UnderlyingType>( newLine - 1 ) );
        filteredView_->trySelectLine( logFilteredData_->getLineIndexNumber( selectedLine ) );
        logMainView_->trySelectLine( selectedLine );
    }
}

//
// Protected functions
//
void CrawlerWidget::doSetData( std::shared_ptr<LogData> logData,
                               std::shared_ptr<LogFilteredData> filteredData )
{
    logData_ = std::move( logData );
    logFilteredData_ = std::move( filteredData );
}

void CrawlerWidget::doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp )
{
    quickFindPattern_ = std::move( qfp );
}

void CrawlerWidget::doSetSavedSearches( SavedSearches* saved_searches )
{
    savedSearches_ = saved_searches;

    // We do setup now, assuming doSetData has been called before
    // us, that's not great really...
    setup();
}

void CrawlerWidget::doSetViewContext( const QString& view_context )
{
    LOG_DEBUG << "CrawlerWidget::doSetViewContext: " << view_context.toLocal8Bit().data();

    const auto context = CrawlerWidgetContext{ view_context };

    setSizes( context.sizes() );
    searchPanel_->matchCaseButton()->setChecked( !context.ignoreCase() );
    searchPanel_->useRegexpButton()->setChecked( context.useRegexp() );
    searchPanel_->inverseButton()->setChecked( context.inverseRegexp() );
    searchPanel_->booleanButton()->setChecked( context.useBooleanCombination() );

    searchPanel_->searchRefreshButton()->setChecked( context.autoRefresh() );
    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( context.autoRefresh() );

    const auto& config = Configuration::get();
    logMainView_->followSet( context.followFile() && config.anyFileWatchEnabled() );

    const auto savedMarks = context.marks();
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines_ ),
                    []( const auto& l ) { return LineNumber( l ); } );
}

std::shared_ptr<const ViewContextInterface> CrawlerWidget::doGetViewContext() const
{
    auto context = std::make_shared<const CrawlerWidgetContext>(
        sizes(), ( !searchPanel_->matchCaseButton()->isChecked() ),
        searchPanel_->searchRefreshButton()->isChecked(), logMainView_->isFollowEnabled(),
        searchPanel_->useRegexpButton()->isChecked(), searchPanel_->inverseButton()->isChecked(),
        searchPanel_->booleanButton()->isChecked(), logFilteredData_->getMarks() );

    return static_cast<std::shared_ptr<const ViewContextInterface>>( context );
}

//
// Q_SLOTS:
//

void CrawlerWidget::startNewSearch()
{
    if ( searchPanel_->keepSearchResultsButton()->isChecked() ) {
        searchPanel_->keepSearchResultsButton()->setChecked( false );

        logFilteredData_->interruptSearch();
        logFilteredData_ = logData_->getNewFilteredData();

        filteredView_ = new FilteredView( logFilteredData_.get(), quickFindPattern_.get() );
        filteredViewsData_[ filteredView_ ] = logFilteredData_;

        connectAllFilteredViewSlots( filteredView_ );

        auto index = tabbedFilteredView_->addTab( filteredView_, "" );
        tabbedFilteredView_->setCurrentIndex( index );

        connect( logFilteredData_.get(), &LogFilteredData::searchProgressed, this,
                 &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

        logMainView_->useNewFiltering( logFilteredData_.get() );

        applyConfiguration();
    }

    const int currentIndex = tabbedFilteredView_->currentIndex();
    tabbedFilteredView_->currentWidget()->setProperty(
        SearchPatternProperty, searchPanel_->searchLineEdit()->currentText() );
    tabbedFilteredView_->setTabText(
        currentIndex, tr( "Find \"%1\"" ).arg( searchPanel_->searchLineEdit()->currentText() ) );

    // Record the search line in the recent list
    // (reload the list first in case another glogg changed it)
    const auto& searches = SavedSearches::getSynced();
    savedSearches_->addRecent( searchPanel_->searchLineEdit()->currentText() );
    searches.save();

    // Update the SearchLine (history)
    updateSearchCombo();
    // Call the private function to do the search
    replaceCurrentSearch( searchPanel_->searchLineEdit()->currentText() );
}

void CrawlerWidget::updatePredefinedFiltersWidget()
{
    searchPanel_->predefinedFilters()->updateSearchPattern(
        searchPanel_->searchLineEdit()->currentText(), searchPanel_->booleanButton()->isChecked() );
}

void CrawlerWidget::stopSearch()
{
    logFilteredData_->interruptSearch();
    searchState_.stopSearch();
    printSearchInfoMessage();
}

void CrawlerWidget::clearSearchHistory()
{
    // Clear line
    searchPanel_->searchLineEdit()->clear();

    // Sync and clear saved searches
    auto& searches = SavedSearches::getSynced();
    savedSearches_->clear();
    searches.save();

    searchPanel_->searchLineCompleter()->setModel(
        new QStringListModel( {}, searchPanel_->searchLineCompleter() ) );
}

void CrawlerWidget::editSearchHistory()
{
    // Sync and clear saved searches
    auto& searches = SavedSearches::getSynced();

    auto history = savedSearches_->recentSearches().join( QChar::LineFeed );
    bool ok;
    QString newHistory = QInputDialog::getMultiLineText(
        this, QApplication::applicationDisplayName(), tr( "Search history:" ), history, &ok );

    if ( ok ) {
        savedSearches_->clear();
        auto items = newHistory.split( QChar::LineFeed, Qt::SkipEmptyParts );

        std::for_each( items.rbegin(), items.rend(), [ this ]( const auto& item ) {
            savedSearches_->addRecent( item );
            LOG_INFO << item;
        } );
    }
    searches.save();

    updateSearchCombo();
}

void CrawlerWidget::saveAsPredefinedFilter()
{
    const auto currentText = searchPanel_->searchLineEdit()->currentText();

    Q_EMIT saveCurrentSearchAsPredefinedFilter( currentText );
}

void CrawlerWidget::showSearchContextMenu()
{
    if ( searchPanel_->searchLineContextMenu() )
        searchPanel_->searchLineContextMenu()->exec( QCursor::pos( activeScreen( this ) ) );
}

// When receiving the 'newDataAvailable' signal from LogFilteredData
void CrawlerWidget::updateFilteredView( LinesCount nbMatches, int progress,
                                        LineNumber initialPosition )
{
    LOG_DEBUG << "updateFilteredView received.";

    searchPanel_->searchInfoLine()->show();

    if ( progress == 100 ) {
        // Searching done
        printSearchInfoMessage( nbMatches );
        // De-activate the stop button
        searchPanel_->stopButton()->setEnabled( false );
        searchPanel_->stopButton()->hide();
        searchPanel_->searchButton()->show();
        searchPanel_->clearButton()->show();
    }
    else {
        // Search in progress
        // We ignore 0% and 100% to avoid a flash when the search is very short
        if ( progress > 0 ) {
            searchInfoState_ = SearchInfoState::Progress;
            searchInfoMatches_ = nbMatches;
            searchInfoProgress_ = progress;
            renderSearchInfoMessage();
        }
    }

    // If more (or less, e.g. come back to 0) matches have been found
    if ( nbMatches != nbMatches_ ) {
        nbMatches_ = nbMatches;

        // Recompute the content of the filtered window.
        filteredView_->updateData();

        // Update the match overview
        overview_.updateData( logData_->getNbLine() );

        // New data found icon
        if ( initialPosition > 0_lnum ) {
            changeDataStatus( DataStatus::NEW_FILTERED_DATA );
        }

        // Also update the top window for the coloured bullets.
        logMainView_->forceRefresh();
        update();
    }

    // Try to restore the filtered window selection close to where it was
    // only for full searches to avoid disconnecting follow mode!
    if ( ( progress == 100 ) && ( initialPosition == searchStartLine_ )
         && ( !isFollowEnabled() ) ) {
        const auto currenLineIndex = logFilteredData_->getLineIndexNumber( currentLineNumber_ );
        LOG_DEBUG << "updateFilteredView: restoring selection: "
                  << " absolute line number (0based) " << currentLineNumber_ << " index "
                  << currenLineIndex;
        filteredView_->selectAndDisplayLine( currenLineIndex );
        filteredView_->setSearchLimits( searchStartLine_, searchEndLine_ );
    }
}

void CrawlerWidget::jumpToMatchingLine( LineNumber filteredLineNb, LinesCount nLines,
                                        LineColumn startCol, LineLength nSymbols )
{
    const auto mainViewLine = logFilteredData_->getMatchingLineNumber( filteredLineNb );
    logMainView_->selectPortionAndDisplayLine( mainViewLine, nLines, startCol,
                                               nSymbols ); // FIXME: should be done with a signal.
}

void CrawlerWidget::updateLineNumberHandler( LineNumber line, LinesCount nLines,
                                             LineColumn startCol, LineLength nSymbols )
{
    currentLineNumber_ = line;
    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

void CrawlerWidget::markLinesFromMain( const klogg::vector<LineNumber>& lines )
{
    klogg::vector<LineNumber> alreadyMarkedLines;
    alreadyMarkedLines.reserve( lines.size() );

    bool markAdded = false;
    for ( const auto& line : lines ) {
        if ( line >= logData_->getNbLine() ) {
            continue;
        }

        if ( !logFilteredData_->lineTypeByLine( line ).testFlag(
                 AbstractLogData::LineTypeFlags::Mark ) ) {
            logFilteredData_->addMark( line );
            markAdded = true;
        }
        else {
            alreadyMarkedLines.push_back( line );
        }
    }

    if ( !markAdded ) {
        for ( const auto& line : alreadyMarkedLines ) {
            logFilteredData_->toggleMark( line );
        }
    }

    // Recompute the content of both window.
    filteredView_->updateData();
    logMainView_->updateData();

    // Update the match overview
    overview_.updateData( logData_->getNbLine() );

    // Also update the top window for the coloured bullets.
    update();
}

void CrawlerWidget::markLinesFromFiltered( const klogg::vector<LineNumber>& lines )
{
    klogg::vector<LineNumber> linesInMain( lines.size() );
    std::transform( lines.cbegin(), lines.cend(), linesInMain.begin(),
                    [ this ]( const auto& filteredLine ) {
                        if ( filteredLine < logData_->getNbLine() ) {
                            return logFilteredData_->getMatchingLineNumber( filteredLine );
                        }
                        else {
                            return maxValue<LineNumber>();
                        }
                    } );

    markLinesFromMain( linesInMain );
}

void CrawlerWidget::applyConfiguration()
{
    const auto& config = Configuration::get();
    QFont font = config.mainFont();

    LOG_DEBUG << "CrawlerWidget::applyConfiguration";

    registerShortcuts();

    // Whatever font we use, we should NOT use kerning
    font.setKerning( false );
    font.setFixedPitch( true );

    // Necessary on systems doing subpixel positionning (e.g. Ubuntu 12.04)
    if ( config.forceFontAntialiasing() ) {
        font.setStyleStrategy( QFont::PreferAntialias );
    }

    font.setBold( config.useBoldFont() );

    if ( config.hideAnsiColorSequences() ) {
        logData_->setPrefilter( AnsiColorSequenceRegex );
    }
    else {
        logData_->setPrefilter( {} );
    }

    const bool lineNumbersVisible = config.lineNumbersVisible();
    logMainView_->setLineNumbersVisible( lineNumbersVisible );

    const auto isFollowModeAllowed = config.anyFileWatchEnabled();
    logMainView_->allowFollowMode( isFollowModeAllowed );
    overview_.setVisible( config.isOverviewVisible() );
    logMainView_->refreshOverview();
    logMainView_->updateFont( font );

    for ( auto i = 0; i < tabbedFilteredView_->count(); ++i ) {
        auto fv = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( i ) );
        fv->setLineNumbersVisible( lineNumbersVisible );
        fv->allowFollowMode( isFollowModeAllowed );
        fv->updateFont( font );
    }

    // Update the SearchLine (history)
    updateSearchCombo();

    FileWatcher::getFileWatcher().updateConfiguration();

    if ( isFollowEnabled() ) {
        changeDataStatus( DataStatus::OLD_DATA );
    }

    reloadPredefinedFilters();
}

void CrawlerWidget::enteringQuickFind()
{
    LOG_DEBUG << "CrawlerWidget::enteringQuickFind";

    // Remember who had the focus (only if it is one of our views)
    QWidget* focus_widget = QApplication::focusWidget();

    if ( ( focus_widget == logMainView_ ) || ( focus_widget == filteredView_ ) )
        qfSavedFocus_ = focus_widget;
    else
        qfSavedFocus_ = nullptr;
}

void CrawlerWidget::exitingQuickFind()
{
    // Restore the focus once the QFBar has been hidden
    if ( qfSavedFocus_ )
        qfSavedFocus_->setFocus();
}

void CrawlerWidget::loadingProgressedHandler( int progress )
{
    loadingInProgress_ = true;
    loadingProgress_ = progress;
    Q_EMIT loadingProgressed( progress );
}

void CrawlerWidget::loadingFinishedHandler( LoadingStatus status )
{
    LOG_INFO << "file loading finished, status " << static_cast<int>( status );

    // We need to refresh the main window because the view lines on the
    // overview have probably changed.
    overview_.updateData( logData_->getNbLine() );

    // FIXME, handle topLine
    // logMainView_->updateData( logData_, topLine );
    logMainView_->updateData();

    // Shall we Forbid starting a search when loading in progress?
    // searchPanel_->searchButton()->setEnabled( false );

    // searchPanel_->searchButton()->setEnabled( true );

    // See if we need to auto-refresh the search
    if ( searchState_.isAutorefreshAllowed() ) {
        searchEndLine_ = LineNumber( logData_->getNbLine().get() );
        if ( searchState_.isFileTruncated() )
            // We need to restart the search
            replaceCurrentSearch( searchPanel_->searchLineEdit()->currentText() );
        else
            logFilteredData_->updateSearch( searchStartLine_, searchEndLine_ );
    }

    // Set the encoding for the views
    updateEncoding();

    clearSearchLimits();

    // Also change the data available icon
    if ( firstLoadDone_ ) {
        changeDataStatus( DataStatus::NEW_DATA );
    }
    else {
        firstLoadDone_ = true;
        for ( const auto& m : savedMarkedLines_ ) {
            logFilteredData_->addMark( m );
        }
        logMainView_->setFocus();
    }

    loadingInProgress_ = false;
    loadingProgress_ = 0;
    Q_EMIT loadingFinished( status );
}

void CrawlerWidget::fileChangedHandler( MonitoredFileStatus status )
{
    // Handle the case where the file has been truncated
    if ( status == MonitoredFileStatus::Truncated ) {
        // Clear all marks (TODO offer the option to keep them)
        logFilteredData_->clearMarks();
        if ( !searchPanel_->searchInfoLine()->text().isEmpty() ) {
            // Invalidate the search
            constexpr auto DropCache = true;
            logFilteredData_->clearSearch( DropCache );
            filteredView_->updateData();
            searchState_.truncateFile();
            printSearchInfoMessage();
            nbMatches_ = 0_lcount;
        }
    }
}

// Returns a pointer to the window in which the search should be done
AbstractLogView* CrawlerWidget::activeView() const
{
    QWidget* activeView;

    // Search in the window that has focus, or the window where 'Find' was
    // called from, or the main window.
    if ( filteredView_->hasFocus() || logMainView_->hasFocus() )
        activeView = QApplication::focusWidget();
    else
        activeView = qfSavedFocus_;

    if ( activeView ) {
        auto* view = qobject_cast<AbstractLogView*>( activeView );
        return view;
    }
    else {
        LOG_WARNING << "No active view, defaulting to logMainView";
        return logMainView_;
    }
}

void CrawlerWidget::searchForward()
{
    LOG_DEBUG << "CrawlerWidget::searchForward";

    activeView()->searchForward();
}

void CrawlerWidget::searchBackward()
{
    LOG_DEBUG << "CrawlerWidget::searchBackward";

    activeView()->searchBackward();
}

void CrawlerWidget::resetStateOnSearchPatternChanges()
{
    // We suspend auto-refresh

    searchState_.changeExpression();
    printSearchInfoMessage( logFilteredData_->getNbMatches() );
}

void CrawlerWidget::searchRefreshChangedHandler( bool isRefreshing )
{
    searchState_.setAutorefresh( isRefreshing );
    printSearchInfoMessage( logFilteredData_->getNbMatches() );
}

void CrawlerWidget::matchCaseChangedHandler( bool shouldMatchCase )
{
    searchPanel_->searchLineCompleter()->setCaseSensitivity(
        shouldMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::booleanCombiningChangedHandler( bool )
{
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::useRegexpChangeHandler( bool )
{
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::searchTextChangeHandler( QString )
{
    resetStateOnSearchPatternChanges();
    updatePredefinedFiltersWidget();
}

void CrawlerWidget::changeFilteredViewVisibility( int index )
{
    QStandardItem* item = searchPanel_->visibilityModel()->item( index );
    auto visibility = item->data().value<FilteredView::Visibility>();

    filteredView_->setVisibility( visibility );

    if ( logFilteredData_->getNbLine() > 0_lcount ) {
        const auto lineIndex = logFilteredData_->getLineIndexNumber( currentLineNumber_ );
        filteredView_->selectAndDisplayLine( lineIndex );
    }
}

void CrawlerWidget::setSearchPatternFromPredefinedFilters( const QList<PredefinedFilter>& filters )
{
    QString searchPattern;
    for ( const auto& filter : filters ) {
        combinePatterns( searchPattern, escapeSearchPattern( filter.pattern, filter.useRegex ) );
    }
    setSearchPattern( searchPattern );
}

QString CrawlerWidget::escapeSearchPattern( const QString& pattern, bool isRegex ) const
{
    auto escapedPattern = ( !isRegex && searchPanel_->useRegexpButton()->isChecked() )
                              ? QRegularExpression::escape( pattern )
                              : pattern;

    if ( searchPanel_->booleanButton()->isChecked() ) {
        escapedPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    return escapedPattern;
}

QString& CrawlerWidget::combinePatterns( QString& currentPattern, const QString& newPattern ) const
{
    if ( !currentPattern.isEmpty() ) {
        if ( searchPanel_->booleanButton()->isChecked() ) {
            currentPattern.append( " or " );
        }
        else if ( searchPanel_->useRegexpButton()->isChecked() ) {
            currentPattern.append( '|' );
        }
    }

    currentPattern.append( newPattern );

    return currentPattern;
}

void CrawlerWidget::addToSearch( const QString& searchString )
{
    const auto newPattern = escapeSearchPattern( searchString );
    QString currentPattern = searchPanel_->searchLineEdit()->currentText();
    setSearchPattern( combinePatterns( currentPattern, newPattern ) );
}

void CrawlerWidget::excludeFromSearch( const QString& searchString )
{
    QString currentPattern = searchPanel_->searchLineEdit()->currentText();

    const auto wasInBooleanCombinationMode = searchPanel_->booleanButton()->isChecked();
    if ( !wasInBooleanCombinationMode ) {
        currentPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    searchPanel_->booleanButton()->setChecked( true );

    const auto newPattern = escapeSearchPattern( searchString );

    if ( !currentPattern.isEmpty() ) {
        currentPattern.append( " and " );
    }

    currentPattern.append( "not(" ).append( newPattern ).append( ')' );
    setSearchPattern( currentPattern );
}

void CrawlerWidget::replaceSearch( const QString& searchString )
{
    setSearchPattern( escapeSearchPattern( searchString ) );
}

void CrawlerWidget::setSearchPattern( const QString& searchPattern )
{
    searchPanel_->searchLineEdit()->setEditText( searchPattern );
    updatePredefinedFiltersWidget();
    // Set the focus to lineEdit so that the user can press 'Return' immediately
    searchPanel_->searchLineEdit()->lineEdit()->setFocus();

    if ( Configuration::get().autoRunSearchOnPatternChange() ) {
        dispatchToMainThread( [ this ] { startNewSearch(); } );
    }
}

void CrawlerWidget::mouseHoveredOverMatch( LineNumber line )
{
    const auto line_in_mainview = logFilteredData_->getMatchingLineNumber( line );

    overviewWidget_->highlightLine( line_in_mainview );
}

void CrawlerWidget::activityDetected()
{
    changeDataStatus( DataStatus::OLD_DATA );
}

void CrawlerWidget::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStartLine_ = startLine;
    searchEndLine_ = endLine;

    logMainView_->setSearchLimits( startLine, endLine );
    filteredView_->setSearchLimits( startLine, endLine );
}

void CrawlerWidget::clearSearchLimits()
{
    setSearchLimits( 0_lnum, LineNumber( logData_->getNbLine().get() ) );
}

//
// Private functions
//

// Build the widget and connect all the signals, this must be done once
// the data are attached.
void CrawlerWidget::setup()
{
    LOG_INFO << "Setup crawler widget";

    assert( logData_ );
    assert( logFilteredData_ );

    // The views

    overviewWidget_ = new OverviewWidget();
    logMainView_
        = new LogMainView( logData_.get(), quickFindPattern_.get(), &overview_, overviewWidget_ );
    logMainView_->setObjectName( QStringLiteral( "logMainView" ) );
    logMainView_->setContentsMargins( 2, 0, 2, 0 );

    filteredView_ = new FilteredView( logFilteredData_.get(), quickFindPattern_.get() );
    filteredView_->setObjectName( QStringLiteral( "logFilteredView" ) );
    filteredViewsData_[ filteredView_ ] = logFilteredData_;
    filteredView_->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_->setOverview( &overview_ );
    overviewWidget_->setParent( logMainView_ );

    // Connect the search to the top view
    logMainView_->useNewFiltering( logFilteredData_.get() );

    searchPanel_ = new SearchPanel( savedSearches_->recentSearches() );
    compose( logMainView_, searchPanel_, filteredView_ );
    tabbedFilteredView_ = resultsTabs();
    setFocusProxy( searchPanel_->searchLineEdit() );
    auto& config = Configuration::get();

    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( searchPanel_->searchRefreshButton()->isChecked() );
    useRegexpChangeHandler( searchPanel_->useRegexpButton()->isChecked() );
    matchCaseChangedHandler( searchPanel_->matchCaseButton()->isChecked() );
    booleanCombiningChangedHandler( searchPanel_->booleanButton()->isChecked() );

    // Default splitter position (usually overridden by the config file)
    setSizes( config.splitterSizes() );

    registerShortcuts();
    loadIcons();

    connect( searchPanel_, &SearchPanel::patternEdited, this,
             &CrawlerWidget::searchTextChangeHandler );
    connect( searchPanel_, &SearchPanel::patternSelected, this,
             &CrawlerWidget::updatePredefinedFiltersWidget );
    connect( searchPanel_, &SearchPanel::filtersChanged, this,
             &CrawlerWidget::setSearchPatternFromPredefinedFilters );
    connect( searchPanel_, &SearchPanel::contextMenuRequested, this,
             &CrawlerWidget::showSearchContextMenu );
    connect( searchPanel_, &SearchPanel::saveFilterRequested, this,
             &CrawlerWidget::saveAsPredefinedFilter );
    connect( searchPanel_, &SearchPanel::clearHistoryRequested, this,
             &CrawlerWidget::clearSearchHistory );
    connect( searchPanel_, &SearchPanel::editHistoryRequested, this,
             &CrawlerWidget::editSearchHistory );
    connect( searchPanel_, &SearchPanel::searchRequested, this, &CrawlerWidget::startNewSearch );
    connect( searchPanel_, &SearchPanel::stopRequested, this, &CrawlerWidget::stopSearch );
    connect( searchPanel_, &SearchPanel::visibilityChanged, this,
             &CrawlerWidget::changeFilteredViewVisibility );

    connect( logMainView_, &LogMainView::newSelection,
             [ this ]( auto ) { logMainView_->update(); } );

    connect( logMainView_, &LogMainView::newSelection, this,
             &CrawlerWidget::updateLineNumberHandler );

    connect( logMainView_, &LogMainView::markLines, this, &CrawlerWidget::markLinesFromMain );

    connect( logMainView_, &LogMainView::highlightersChange, this,
             &CrawlerWidget::applyConfiguration );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    // Follow option (up and down)
    connect( this, &CrawlerWidget::followSet, logMainView_, &LogMainView::followSet );
    connect( logMainView_, &LogMainView::followModeChanged, this,
             &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, logMainView_, &LogMainView::textWrapSet );

    // Detect activity in the views
    connect( logMainView_, &LogMainView::activity, this, &CrawlerWidget::activityDetected );

    connect( logMainView_, &LogMainView::changeSearchLimits, this,
             &CrawlerWidget::setSearchLimits );

    connect( logMainView_, &LogMainView::clearSearchLimits, this,
             &CrawlerWidget::clearSearchLimits );

    connect( tabbedFilteredView_, &QTabWidget::currentChanged, this,
             &CrawlerWidget::changeFilteredView );

    connect( tabbedFilteredView_, &QTabWidget::tabCloseRequested, this,
             &CrawlerWidget::closeFilteredView );

    connect( logMainView_, &LogMainView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( logMainView_, &LogMainView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( logFilteredData_.get(), &LogFilteredData::searchProgressed, this,
             &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

    // Sent load file update to MainWindow (for status update)
    connect( logData_.get(), &LogData::loadingProgressed, this,
             &CrawlerWidget::loadingProgressedHandler );
    connect( logData_.get(), &LogData::loadingFinished, this,
             &CrawlerWidget::loadingFinishedHandler );
    connect( logData_.get(), &LogData::fileChanged, this, &CrawlerWidget::fileChangedHandler );

    // Search auto-refresh
    connect( searchPanel_, &SearchPanel::autoRefreshChanged, this,
             &CrawlerWidget::searchRefreshChangedHandler );

    connect( searchPanel_, &SearchPanel::matchCaseChanged, this,
             &CrawlerWidget::matchCaseChangedHandler );

    connect( searchPanel_, &SearchPanel::regexpChanged, this,
             &CrawlerWidget::useRegexpChangeHandler );

    connect( searchPanel_, &SearchPanel::booleanChanged, this,
             &CrawlerWidget::booleanCombiningChangedHandler );

    // Advise the parent the checkboxes have been changed
    // (for maintaining default config)
    connect( searchPanel_, &SearchPanel::autoRefreshChanged, this,
             &CrawlerWidget::searchRefreshChanged );
    connect( searchPanel_, &SearchPanel::matchCaseChanged, this, &CrawlerWidget::matchCaseChanged );

    // Switch between views
    connect( logMainView_, &AbstractLogView::clearColorLabels, this,
             &CrawlerWidget::clearColorLabels );

    connect( logMainView_, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( logMainView_, &AbstractLogView::sendSelectionToScratchpad, this,
             [ this ]() { Q_EMIT sendToScratchpad( logMainView_->getSelectedText() ); } );

    connect( logMainView_, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ this ]() { Q_EMIT replaceDataInScratchpad( logMainView_->getSelectedText() ); } );

    connectAllFilteredViewSlots( filteredView_ );

    const auto defaultEncodingMib = config.defaultEncodingMib();
    if ( defaultEncodingMib >= 0 ) {
        encodingMib_ = defaultEncodingMib;
    }
    updatePredefinedFiltersWidget();
}

void CrawlerWidget::changeFilteredView( int tabIndex )
{
    logFilteredData_->interruptSearch();
    if ( tabIndex >= 0 ) {
        auto* tabFilteredView
            = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );

        filteredView_ = tabFilteredView;
        logFilteredData_ = filteredViewsData_.at( tabFilteredView );

        Q_EMIT filteredViewChanged();

        logMainView_->useNewFiltering( logFilteredData_.get() );
        changeFilteredViewVisibility( searchPanel_->visibilityBox()->currentIndex() );
    }
}

void CrawlerWidget::closeFilteredView( int tabIndex )
{
    auto* tabFilteredView = tabbedFilteredView_->widget( tabIndex );
    connect( tabFilteredView, &QObject::destroyed, this, &CrawlerWidget::filteredViewDestroyed );
    tabFilteredView->deleteLater();
}

void CrawlerWidget::filteredViewDestroyed( QObject* view )
{
    filteredViewsData_.erase( qobject_cast<FilteredView*>( view ) );
}

void CrawlerWidget::saveSplitterSizes() const
{
    LOG_INFO << "Saving default splitter size";
    auto& splitterConfig = Configuration::get();
    splitterConfig.setSplitterSizes( sizes() );
    splitterConfig.save();
}

void CrawlerWidget::changeFontSize( bool increase )
{
    auto& fontConfig = Configuration::get();

    auto fontInfo = QFontInfo( fontConfig.mainFont() );
    const auto availableSizes = FontUtils::availableFontSizes( fontInfo.family() );

    auto currentSize
        = std::find( availableSizes.cbegin(), availableSizes.cend(), fontInfo.pointSize() );
    if ( increase && currentSize != std::prev( availableSizes.cend() ) ) {
        currentSize = std::next( currentSize );
    }
    else if ( !increase && currentSize != availableSizes.begin() ) {
        currentSize = std::prev( currentSize );
    }

    if ( currentSize != availableSizes.cend() ) {
        QFont newFont{ fontInfo.family(), *currentSize };

        fontConfig.setMainFont( newFont );
        logMainView_->updateFont( newFont );
        filteredView_->updateFont( newFont );
    }
}

void CrawlerWidget::connectAllFilteredViewSlots( FilteredView* view )
{
    connect( view, &FilteredView::newSelection, view, [ view ]( auto ) { view->update(); } );

    connect( view, &FilteredView::newSelection, this, &CrawlerWidget::jumpToMatchingLine );

    connect( view, &FilteredView::markLines, this, &CrawlerWidget::markLinesFromFiltered );

    connect( view, &FilteredView::highlightersChange, this, &CrawlerWidget::applyConfiguration );

    connect( view, QOverload<const QString&>::of( &FilteredView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    connect( view, &FilteredView::mouseHoveredOverLine, this,
             &CrawlerWidget::mouseHoveredOverMatch );

    connect( view, &FilteredView::mouseLeftHoveringZone, overviewWidget_,
             &OverviewWidget::removeHighlight );

    connect( this, &CrawlerWidget::followSet, view, &FilteredView::followSet );

    connect( view, &FilteredView::followModeChanged, this, &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, view, &FilteredView::textWrapSet );

    connect( view, &FilteredView::activity, this, &CrawlerWidget::activityDetected );

    connect( view, &FilteredView::changeSearchLimits, this, &CrawlerWidget::setSearchLimits );

    connect( view, &FilteredView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( view, &FilteredView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( view, &FilteredView::clearSearchLimits, this, &CrawlerWidget::clearSearchLimits );

    connect( view, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( view, &AbstractLogView::sendSelectionToScratchpad, this,
             [ view, this ]() { Q_EMIT sendToScratchpad( view->getSelectedText() ); } );

    connect( view, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ view, this ]() { Q_EMIT replaceDataInScratchpad( view->getSelectedText() ); } );

    connect( view, &FilteredView::exitView, logMainView_,
             QOverload<>::of( &LogMainView::setFocus ) );

    connect( view, &AbstractLogView::clearColorLabels, this, &CrawlerWidget::clearColorLabels );

    connect( logMainView_, &LogMainView::exitView, view,
             QOverload<>::of( &FilteredView::setFocus ) );
}

void CrawlerWidget::registerShortcuts()
{
    LOG_INFO << "registering shortcuts for crawler widget";

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();

    const auto& config = Configuration::get();
    const auto& configuredShortcuts = config.shortcuts();

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityForward, [ this ]() {
                                          searchPanel_->visibilityBox()->setCurrentIndex(
                                              ( searchPanel_->visibilityBox()->currentIndex() + 1 )
                                              % searchPanel_->visibilityBox()->count() );
                                      } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerEnableCaseMatching,
                                      [ this ]() { searchPanel_->matchCaseButton()->toggle(); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerEnableRegex,
                                      [ this ]() { searchPanel_->useRegexpButton()->toggle(); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerEnableInverseMatching,
                                      [ this ]() { searchPanel_->inverseButton()->toggle(); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerEnableRegexCombining,
                                      [ this ]() { searchPanel_->booleanButton()->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableAutoRefresh,
        [ this ]() { searchPanel_->searchRefreshButton()->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerKeepResults,
        [ this ]() { searchPanel_->keepSearchResultsButton()->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityBackward, [ this ]() {
            int nextIndex = searchPanel_->visibilityBox()->currentIndex() - 1;
            if ( nextIndex < 0 ) {
                nextIndex = searchPanel_->visibilityBox()->count() - 1;
            }
            searchPanel_->visibilityBox()->setCurrentIndex( nextIndex );
        } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMarksAndMatches, [ this ]() {
            if ( searchPanel_->visibilityBox()->count() > 0 ) {
                searchPanel_->visibilityBox()->setCurrentIndex( 0 );
            }
        } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityToMarks, [ this ]() {
                                          if ( searchPanel_->visibilityBox()->count() > 1 ) {
                                              searchPanel_->visibilityBox()->setCurrentIndex( 1 );
                                          }
                                      } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityToMatches, [ this ]() {
                                          if ( searchPanel_->visibilityBox()->count() > 2 ) {
                                              searchPanel_->visibilityBox()->setCurrentIndex( 2 );
                                          }
                                      } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerIncreseTopViewSize, [ this ]() { changeTopViewSize( 1 ); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerDecreaseTopViewSize, [ this ]() { changeTopViewSize( -1 ); } );

    const auto exitSearchKeySequence = QKeySequence( QKeySequence::Cancel );
    ShortcutAction::registerShortcut( exitSearchKeySequence.toString(), shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut, [ this ]() {
                                          const auto activeView = this->activeView();
                                          if ( activeView ) {
                                              activeView->setFocus();
                                          }
                                      } );

    std::array<std::string, 9> colorLables = {
        ShortcutAction::LogViewAddColorLabel1, ShortcutAction::LogViewAddColorLabel2,
        ShortcutAction::LogViewAddColorLabel3, ShortcutAction::LogViewAddColorLabel4,
        ShortcutAction::LogViewAddColorLabel5, ShortcutAction::LogViewAddColorLabel6,
        ShortcutAction::LogViewAddColorLabel7, ShortcutAction::LogViewAddColorLabel8,
        ShortcutAction::LogViewAddColorLabel9,
    };

    for ( auto label = 0u; label < colorLables.size(); ++label ) {
        ShortcutAction::registerShortcut(
            configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
            colorLables[ label ], [ this, label ]() { addColorLabelToSelection( label ); } );
    }

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewAddNextColorLabel, [ this ]() { addNextColorLabelToSelection(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewClearColorLabels, [ this ]() { clearColorLabels(); } );

    logMainView_->registerShortcuts();
    filteredView_->registerShortcuts();
}

void CrawlerWidget::loadIcons()
{
    searchPanel_->loadIcons( iconLoader_ );
}

// Create a new search using the text passed, replace the currently
// used one and destroy the old one.
void CrawlerWidget::replaceCurrentSearch( const QString& searchText )
{
    LOG_INFO << "replacing current search with " << searchText;
    // Interrupt the search if it's ongoing
    logFilteredData_->interruptSearch();

    // We have to wait for the last search update (100%)
    // before clearing/restarting to avoid having remaining results.

    // FIXME: this is a bit of a hack, we call processEvents
    // for Qt to empty its event queue, including (hopefully)
    // the search update event sent by logFilteredData_. It saves
    // us the overhead of having proper sync.
    QApplication::processEvents( QEventLoop::ExcludeUserInputEvents );

    nbMatches_ = 0_lcount;

    // Switch to "Marks and matches" view when in "Marks" view
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    if ( !filteredView_->visibility().testFlag( VisibilityFlags::Matches ) ) {
        searchPanel_->visibilityBox()->setCurrentIndex( 0 );
    }

    // Clear and recompute the content of the filtered window.
    logFilteredData_->clearSearch();
    filteredView_->updateData();

    // Update the match overview
    overview_.updateData( logData_->getNbLine() );

    if ( !searchText.isEmpty() ) {

        // Constructs the regexp
        auto regexpPattern = RegularExpressionPattern(
            searchText, searchPanel_->matchCaseButton()->isChecked(),
            searchPanel_->inverseButton()->isChecked(), searchPanel_->booleanButton()->isChecked(),
            !searchPanel_->useRegexpButton()->isChecked() );

        RegularExpression hsExpression{ regexpPattern };
        auto isValidExpression = hsExpression.isValid();

        if ( isValidExpression ) {
            // Activate the stop button
            searchPanel_->stopButton()->setEnabled( true );
            searchPanel_->stopButton()->show();
            searchPanel_->clearButton()->hide();
            searchPanel_->searchButton()->hide();
            // Start a new asynchronous search
            logFilteredData_->runSearch( regexpPattern, searchStartLine_, searchEndLine_ );
            // Accept auto-refresh of the search
            searchState_.startSearch();
            searchInfoState_ = SearchInfoState::Empty;
            searchExpressionError_.clear();
            renderSearchInfoMessage();
            logMainView_->setSearchPattern( regexpPattern );
            filteredView_->setSearchPattern( regexpPattern );
        }
        else {
            // The regexp is wrong
            logFilteredData_->clearSearch();
            filteredView_->updateData();
            searchState_.resetState();

            // Inform the user
            searchInfoState_ = SearchInfoState::InvalidExpression;
            searchExpressionError_ = hsExpression.errorString();
            renderSearchInfoMessage();

            logMainView_->setSearchPattern( {} );
            filteredView_->setSearchPattern( {} );
        }
    }
    else {
        searchState_.resetState();
        printSearchInfoMessage();
    }
}

// Updates the content of the drop down list for the saved searches,
// called when the SavedSearch has been changed.
void CrawlerWidget::updateSearchCombo()
{
    searchPanel_->updateHistory( savedSearches_->recentSearches() );
}

// Print the search info message.
void CrawlerWidget::printSearchInfoMessage( LinesCount nbMatches )
{
    switch ( searchState_.getState() ) {
    case SearchState::NoSearch:
        searchInfoState_ = SearchInfoState::Empty;
        break;
    case SearchState::Static:
    case SearchState::Autorefreshing:
        searchInfoState_ = SearchInfoState::Matches;
        searchInfoMatches_ = nbMatches;
        break;
    case SearchState::FileTruncated:
    case SearchState::TruncatedAutorefreshing:
        searchInfoState_ = SearchInfoState::FileTruncated;
        break;
    }

    renderSearchInfoMessage();
}

void CrawlerWidget::renderSearchInfoMessage()
{
    QString text;
    switch ( searchInfoState_ ) {
    case SearchInfoState::Empty:
        break;
    case SearchInfoState::Matches:
        text = searchInfoMatches_.get() > 1
                   ? tr( "%1 matches found" ).arg( searchInfoMatches_.get() )
                   : tr( "%1 match found" ).arg( searchInfoMatches_.get() );
        break;
    case SearchInfoState::FileTruncated:
        text = tr( "File truncated on disk" );
        break;
    case SearchInfoState::Progress:
        text = tr( "Search in progress (%1 %)..." ).arg( searchInfoProgress_ )
               + ( searchInfoMatches_.get() > 1
                       ? tr( " %1 matches found so far." ).arg( searchInfoMatches_.get() )
                       : tr( " %1 match found so far." ).arg( searchInfoMatches_.get() ) );
        break;
    case SearchInfoState::InvalidExpression:
        text = tr( "Error in expression: %1" ).arg( searchExpressionError_ );
        break;
    }

    searchPanel_->searchInfoLine()->hideGauge();
    searchPanel_->searchInfoLine()->setPalette(
        searchInfoState_ == SearchInfoState::InvalidExpression ? ErrorPalette : QPalette{} );
    searchPanel_->searchInfoLine()->setText( text );
    searchPanel_->searchInfoLine()->setVisible( !text.isEmpty() );
    if ( searchInfoState_ == SearchInfoState::Progress ) {
        searchPanel_->searchInfoLine()->displayGauge( searchInfoProgress_ );
    }
}

void CrawlerWidget::retranslateSearchResultTitles()
{
    for ( int index = 0; index < tabbedFilteredView_->count(); ++index ) {
        const QVariant searchPattern
            = tabbedFilteredView_->widget( index )->property( SearchPatternProperty );
        if ( searchPattern.isValid() ) {
            tabbedFilteredView_->setTabText( index,
                                             tr( "Find \"%1\"" ).arg( searchPattern.toString() ) );
        }
    }
}

// Change the data status and, if needed, advise upstream.
void CrawlerWidget::changeDataStatus( DataStatus status )
{
    if ( ( status != dataStatus_ )
         && ( !( dataStatus_ == DataStatus::NEW_FILTERED_DATA
                 && status == DataStatus::NEW_DATA ) ) ) {
        dataStatus_ = status;
        Q_EMIT dataStatusChanged( dataStatus_ );
    }
}

void CrawlerWidget::retranslateUi()
{
    searchPanel_->retranslateUi();
    retranslateSearchResultTitles();
    renderSearchInfoMessage();
    updateEncodingText();
}

void CrawlerWidget::updateEncodingText()
{
    const QTextCodec* textCodec = [ this ]() {
        QTextCodec* codec = nullptr;
        if ( !encodingMib_ ) {
            codec = logData_->getDetectedEncoding();
        }
        else {
            codec = QTextCodec::codecForMib( *encodingMib_ );
        }
        return codec ? codec : QTextCodec::codecForLocale();
    }();

    const QString encodingPrefix = encodingMib_ ? tr( "Displayed as %1" ) : tr( "Detected as %1" );
    encodingText_ = encodingPrefix.arg( textCodec->name().constData() );
}

// Determine the right encoding and set the views.
void CrawlerWidget::updateEncoding()
{
    const QTextCodec* textCodec = [ this ]() {
        QTextCodec* codec = nullptr;
        if ( !encodingMib_ ) {
            codec = logData_->getDetectedEncoding();
        }
        else {
            codec = QTextCodec::codecForMib( *encodingMib_ );
        }
        return codec ? codec : QTextCodec::codecForLocale();
    }();

    updateEncodingText();

    logData_->interruptLoading();

    logData_->setDisplayEncoding( textCodec->name().constData() );
    logMainView_->forceRefresh();
    logFilteredData_->setDisplayEncoding( textCodec->name().constData() );
    filteredView_->forceRefresh();
}

// Change the respective size of the two views
void CrawlerWidget::changeTopViewSize( int32_t delta )
{
    int min, max;
    getRange( 1, &min, &max );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 ) << " " << min << " " << max;
    moveSplitter( closestLegalPosition( sizes().at( 0 ) + ( delta * 10 ), 1 ), 1 );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 );
}

void CrawlerWidget::addColorLabelToSelection( size_t label )
{
    updateColorLabels( colorLabelsManager_.setColorLabel( label, getSelectedText() ) );
}

void CrawlerWidget::addNextColorLabelToSelection()
{
    updateColorLabels( colorLabelsManager_.setNextColorLabel( getSelectedText() ) );
}

void CrawlerWidget::clearColorLabels()
{
    updateColorLabels( colorLabelsManager_.clear() );
}

void CrawlerWidget::updateColorLabels(
    const ColorLabelsManager::QuickHighlightersCollection& labels )
{
    logMainView_->setQuickHighlighters( labels );
    filteredView_->setQuickHighlighters( labels );
}

//
// SearchState implementation
//
void CrawlerWidget::SearchState::resetState()
{
    state_ = NoSearch;
}

void CrawlerWidget::SearchState::setAutorefresh( bool refresh )
{
    autoRefreshRequested_ = refresh;

    if ( refresh ) {
        if ( state_ == Static )
            state_ = Autorefreshing;
        /*
        else if ( state_ == FileTruncated )
            state_ = TruncatedAutorefreshing;
        */
    }
    else {
        if ( state_ == Autorefreshing )
            state_ = Static;
        else if ( state_ == TruncatedAutorefreshing )
            state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::truncateFile()
{
    if ( state_ == Autorefreshing || state_ == TruncatedAutorefreshing ) {
        state_ = TruncatedAutorefreshing;
    }
    else {
        state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::changeExpression()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::stopSearch()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::startSearch()
{
    if ( autoRefreshRequested_ )
        state_ = Autorefreshing;
    else
        state_ = Static;
}

/*
 * CrawlerWidgetContext
 */
CrawlerWidgetContext::CrawlerWidgetContext( const QString& string )
{
    if ( string.startsWith( '{' ) ) {
        loadFromJson( string );
    }
    else {
        loadFromString( string );
    }
}

void CrawlerWidgetContext::loadFromString( const QString& string )
{
    QRegularExpression regex( "S(\\d+):(\\d+)" );
    QRegularExpressionMatch match = regex.match( string );
    if ( match.hasMatch() ) {
        sizes_ = { match.captured( 1 ).toInt(), match.captured( 2 ).toInt() };
        LOG_DEBUG << "sizes_: " << sizes_[ 0 ] << " " << sizes_[ 1 ];
    }
    else {
        LOG_WARNING << "Unrecognised view size: " << string.toLocal8Bit().data();

        // Default values;
        sizes_ = { 400, 100 };
    }

    QRegularExpression case_refresh_regex( "IC(\\d+):AR(\\d+)" );
    match = case_refresh_regex.match( string );
    if ( match.hasMatch() ) {
        ignoreCase_ = ( match.captured( 1 ).toInt() == 1 );
        autoRefresh_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "ignore_case_: " << ignoreCase_ << " auto_refresh_: " << autoRefresh_;
    }
    else {
        LOG_WARNING << "Unrecognised case/refresh: " << string.toLocal8Bit().data();
        ignoreCase_ = false;
        autoRefresh_ = false;
    }

    QRegularExpression follow_regex( "AR(\\d+):FF(\\d+)" );
    match = follow_regex.match( string );
    if ( match.hasMatch() ) {
        followFile_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "follow_file_: " << followFile_;
    }
    else {
        LOG_WARNING << "Unrecognised follow file " << string.toLocal8Bit().data();
        followFile_ = false;
    }

    useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
}

void CrawlerWidgetContext::loadFromJson( const QString& json )
{
    const auto properties = QJsonDocument::fromJson( json.toLatin1() ).toVariant().toMap();

    if ( properties.contains( "S" ) ) {
        const auto sizes = properties.value( "S" ).toList();
        for ( const auto& s : sizes ) {
            sizes_.append( s.toInt() );
        }
    }

    ignoreCase_ = properties.value( "IC" ).toBool();
    autoRefresh_ = properties.value( "AR" ).toBool();
    followFile_ = properties.value( "FF" ).toBool();
    if ( properties.contains( "RE" ) ) {
        useRegexp_ = properties.value( "RE" ).toBool();
    }
    else {
        useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
    }

    if ( properties.contains( "IR" ) ) {
        inverseRegexp_ = properties.value( "IR" ).toBool();
    }
    else {
        inverseRegexp_ = false;
    }

    if ( properties.contains( "BC" ) ) {
        useBooleanCombination_ = properties.value( "BC" ).toBool();
    }
    else {
        useBooleanCombination_ = false;
    }

    if ( properties.contains( "M" ) ) {
        const auto marks = properties.value( "M" ).toList();
        for ( const auto& m : marks ) {
            marks_.append( m.toUInt() );
        }
    }
}

QString CrawlerWidgetContext::toString() const
{
    const auto toVariantList = []( const auto& list ) -> QVariantList {
        QVariantList variantList;
        for ( const auto& item : list ) {
            variantList.append( static_cast<qulonglong>( item ) );
        }
        return variantList;
    };

    QVariantMap properies;

    properies[ "S" ] = toVariantList( sizes_ );
    properies[ "IC" ] = ignoreCase_;
    properies[ "AR" ] = autoRefresh_;
    properies[ "FF" ] = followFile_;
    properies[ "RE" ] = useRegexp_;
    properies[ "IR" ] = inverseRegexp_;
    properies[ "BC" ] = useBooleanCombination_;
    properies[ "M" ] = toVariantList( marks_ );

    return QJsonDocument::fromVariant( properies ).toJson( QJsonDocument::Compact );
}
