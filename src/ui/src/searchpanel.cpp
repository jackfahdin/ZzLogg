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

// Search control ownership and presentation, independent of log search tasks.

#include "searchpanel.h"
#include "configuration.h"
#include "filteredview.h"
#include "iconloader.h"
#include "infoline.h"
#include <QCompleter>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QStringListModel>
#include <limits>

SearchPanel::SearchPanel( const QStringList& history, QWidget* parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "searchPanel" ) );
    setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );
    // Construct the visibility button
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    visibilityModel_ = new QStandardItemModel( this );

    QStandardItem* marksAndMatchesItem
        = new QStandardItem( QCoreApplication::translate( "CrawlerWidget", "Marks and matches" ) );
    marksAndMatchesItem->setData(
        QVariant::fromValue( VisibilityFlags::Marks | VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( marksAndMatchesItem );

    QStandardItem* marksItem
        = new QStandardItem( QCoreApplication::translate( "CrawlerWidget", "Marks" ) );
    marksItem->setData( QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Marks ) );
    visibilityModel_->appendRow( marksItem );

    QStandardItem* matchesItem
        = new QStandardItem( QCoreApplication::translate( "CrawlerWidget", "Matches" ) );
    matchesItem->setData(
        QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( matchesItem );

    auto* visibilityView = new QListView( this );
    visibilityView->setMovement( QListView::Static );
    // visibilityView->setMinimumWidth( 170 ); // Only needed with custom style-sheet

    visibilityBox_ = new QComboBox();
    visibilityBox_->setModel( visibilityModel_ );
    visibilityBox_->setView( visibilityView );

    // Select "Marks and matches" by default (same default as the filtered view)
    visibilityBox_->setCurrentIndex( 0 );
    visibilityBox_->setContentsMargins( 2, 2, 2, 2 );

    // TODO: Maybe there is some way to set the popup width to be
    // sized-to-content (as it is when the stylesheet is not overriden) in the
    // stylesheet as opposed to setting a hard min-width on the view above.
    /*visibilityBox_->setStyleSheet( " \
        QComboBox:on {\
            padding: 1px 2px 1px 6px;\
            width: 19px;\
        } \
        QComboBox:!on {\
            padding: 1px 2px 1px 7px;\
            width: 19px;\
            height: 16px;\
            border: 1px solid gray;\
        } \
        QComboBox::drop-down::down-arrow {\
            width: 0px;\
            border-width: 0px;\
        } \
" );*/

    // Construct the Search Info line
    searchInfoLine_ = new InfoLine();
    searchInfoLine_->setFrameStyle( QFrame::StyledPanel );
    searchInfoLine_->setFrameShadow( QFrame::Sunken );
    searchInfoLine_->setLineWidth( 1 );
    searchInfoLine_->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Minimum );
    auto searchInfoLineSizePolicy = searchInfoLine_->sizePolicy();
    searchInfoLineSizePolicy.setRetainSizeWhenHidden( false );
    searchInfoLine_->setSizePolicy( searchInfoLineSizePolicy );
    searchInfoLine_->setContentsMargins( 2, 2, 2, 2 );

    matchCaseButton_ = new QToolButton();
    matchCaseButton_->setObjectName( QStringLiteral( "matchCaseButton" ) );
    matchCaseButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Match case" ) );
    matchCaseButton_->setCheckable( true );
    matchCaseButton_->setFocusPolicy( Qt::NoFocus );
    matchCaseButton_->setContentsMargins( 2, 2, 2, 2 );

    useRegexpButton_ = new QToolButton();
    useRegexpButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Use regex" ) );
    useRegexpButton_->setCheckable( true );
    useRegexpButton_->setFocusPolicy( Qt::NoFocus );
    useRegexpButton_->setContentsMargins( 2, 2, 2, 2 );

    inverseButton_ = new QToolButton();
    inverseButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Inverse match" ) );
    inverseButton_->setCheckable( true );
    inverseButton_->setFocusPolicy( Qt::NoFocus );
    inverseButton_->setContentsMargins( 2, 2, 2, 2 );

    booleanButton_ = new QToolButton();
    booleanButton_->setToolTip( QCoreApplication::translate(
        "CrawlerWidget", "Enable regular expression logical combining" ) );
    booleanButton_->setCheckable( true );
    booleanButton_->setFocusPolicy( Qt::NoFocus );
    booleanButton_->setContentsMargins( 2, 2, 2, 2 );

    searchRefreshButton_ = new QToolButton();
    searchRefreshButton_->setObjectName( QStringLiteral( "searchRefreshButton" ) );
    searchRefreshButton_->setToolTip(
        QCoreApplication::translate( "CrawlerWidget", "Auto-refresh" ) );
    searchRefreshButton_->setCheckable( true );
    searchRefreshButton_->setFocusPolicy( Qt::NoFocus );
    searchRefreshButton_->setContentsMargins( 2, 2, 2, 2 );

    // Construct the Search line
    searchLineCompleter_ = new QCompleter( history, this );
    searchLineEdit_ = new QComboBox;
    searchLineEdit_->setObjectName( QStringLiteral( "mainSearchEdit" ) );
    searchLineEdit_->setEditable( true );
    searchLineEdit_->setCompleter( searchLineCompleter_ );
    searchLineEdit_->addItems( history );
    searchLineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    searchLineEdit_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    searchLineEdit_->lineEdit()->setMaxLength( std::numeric_limits<int>::max() / 1024 );
    searchLineEdit_->setContentsMargins( 2, 2, 2, 2 );

    clearSearchHistoryAction_ = new QAction(
        QCoreApplication::translate( "CrawlerWidget", "Clear search history" ), this );
    clearSearchHistoryAction_->setObjectName( QStringLiteral( "clearSearchHistoryAction" ) );
    editSearchHistoryAction_ = new QAction(
        QCoreApplication::translate( "CrawlerWidget", "Edit search history" ), this );
    editSearchHistoryAction_->setObjectName( QStringLiteral( "editSearchHistoryAction" ) );
    saveAsPredefinedFilterAction_
        = new QAction( QCoreApplication::translate( "CrawlerWidget", "Save as Filter" ), this );
    saveAsPredefinedFilterAction_->setObjectName(
        QStringLiteral( "saveAsPredefinedFilterAction" ) );

    searchLineContextMenu_ = searchLineEdit_->lineEdit()->createStandardContextMenu();
    searchLineContextMenu_->setParent( this, Qt::Popup );
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( saveAsPredefinedFilterAction_ );
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( editSearchHistoryAction_ );
    searchLineContextMenu_->addAction( clearSearchHistoryAction_ );
    searchLineEdit_->setContextMenuPolicy( Qt::CustomContextMenu );

    setFocusProxy( searchLineEdit_ );

    clearButton_ = new QToolButton();
    clearButton_->setObjectName( QStringLiteral( "clearSearchButton" ) );
    clearButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Clear search text" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setContentsMargins( 2, 2, 2, 2 );

    searchButton_ = new QToolButton();
    searchButton_->setObjectName( QStringLiteral( "mainSearchButton" ) );
    searchButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Search" ) );
    searchButton_->setAutoRaise( true );
    searchButton_->setContentsMargins( 2, 2, 2, 2 );

    keepSearchResultsButton_ = new QToolButton();
    keepSearchResultsButton_->setObjectName( QStringLiteral( "keepSearchResultsButton" ) );
    keepSearchResultsButton_->setText(
        QCoreApplication::translate( "CrawlerWidget", "Keep Results" ) );
    keepSearchResultsButton_->setToolTip( QCoreApplication::translate(
        "CrawlerWidget", "Keep these results and show subsequent results in a new window" ) );
    keepSearchResultsButton_->setCheckable( true );
    keepSearchResultsButton_->setContentsMargins( 2, 2, 2, 2 );

    stopButton_ = new QToolButton();
    stopButton_->setAutoRaise( true );
    stopButton_->setEnabled( false );
    stopButton_->setVisible( false );
    stopButton_->setContentsMargins( 2, 2, 2, 2 );

    predefinedFilters_ = new PredefinedFiltersComboBox( this );
    predefinedFilters_->setObjectName( QStringLiteral( "predefinedFilters" ) );

    auto* searchLineLayout = new QHBoxLayout( this );
    searchLineLayout->setContentsMargins( 2, 2, 2, 2 );

    searchLineLayout->addWidget( visibilityBox_ );
    searchLineLayout->addWidget( matchCaseButton_ );
    searchLineLayout->addWidget( useRegexpButton_ );
    searchLineLayout->addWidget( inverseButton_ );
    searchLineLayout->addWidget( booleanButton_ );
    searchLineLayout->addWidget( searchRefreshButton_ );
    searchLineLayout->addWidget( predefinedFilters_ );
    searchLineLayout->addWidget( searchLineEdit_ );
    searchLineLayout->addWidget( clearButton_ );
    searchLineLayout->addWidget( searchButton_ );
    searchLineLayout->addWidget( keepSearchResultsButton_ );
    searchLineLayout->addWidget( stopButton_ );
    searchLineLayout->addWidget( searchInfoLine_ );

    visibilityBox_->setObjectName( QStringLiteral( "searchVisibility" ) );
    useRegexpButton_->setObjectName( QStringLiteral( "useRegexpButton" ) );
    inverseButton_->setObjectName( QStringLiteral( "inverseButton" ) );
    booleanButton_->setObjectName( QStringLiteral( "booleanButton" ) );
    stopButton_->setObjectName( QStringLiteral( "stopSearchButton" ) );

    const auto& config = Configuration::get();
    searchRefreshButton_->setChecked( config.isSearchAutoRefreshDefault() );
    matchCaseButton_->setChecked( !config.isSearchIgnoreCaseDefault() );
    useRegexpButton_->setChecked( config.mainRegexpType() == SearchRegexpType::ExtendedRegexp );
    booleanButton_->setChecked( config.isSearchLogicalCombiningDefault() );

    connect( searchLineEdit_->lineEdit(), &QLineEdit::returnPressed, searchButton_,
             &QToolButton::click );
    connect( searchLineEdit_->lineEdit(), &QLineEdit::textEdited, this,
             &SearchPanel::patternEdited );
    connect( searchLineEdit_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &SearchPanel::patternSelected );
    connect( predefinedFilters_, &PredefinedFiltersComboBox::filterChanged, this,
             &SearchPanel::filtersChanged );
    connect( searchLineEdit_, &QWidget::customContextMenuRequested, this,
             &SearchPanel::contextMenuRequested );
    connect( saveAsPredefinedFilterAction_, &QAction::triggered, this,
             &SearchPanel::saveFilterRequested );
    connect( clearSearchHistoryAction_, &QAction::triggered, this,
             &SearchPanel::clearHistoryRequested );
    connect( editSearchHistoryAction_, &QAction::triggered, this,
             &SearchPanel::editHistoryRequested );
    connect( searchButton_, &QToolButton::clicked, this, &SearchPanel::searchRequested );
    connect( stopButton_, &QToolButton::clicked, this, &SearchPanel::stopRequested );
    connect( clearButton_, &QToolButton::clicked, searchLineEdit_, &QComboBox::clearEditText );
    connect( visibilityBox_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &SearchPanel::visibilityChanged );
    connect( searchRefreshButton_, &QToolButton::toggled, this, &SearchPanel::autoRefreshChanged );
    connect( matchCaseButton_, &QToolButton::toggled, this, &SearchPanel::matchCaseChanged );
    connect( useRegexpButton_, &QToolButton::toggled, this, &SearchPanel::regexpChanged );
    connect( booleanButton_, &QToolButton::toggled, this, &SearchPanel::booleanChanged );
}

void SearchPanel::updateHistory( const QStringList& history )
{
    const QString text = searchLineEdit_->currentText();
    searchLineEdit_->clear();
    searchLineEdit_->addItems( history );
    searchLineEdit_->setEditText( text );
    searchLineCompleter_->setModel( new QStringListModel( history, searchLineCompleter_ ) );
}

void SearchPanel::retranslateUi()
{
    visibilityBox_->setItemText(
        0, QCoreApplication::translate( "CrawlerWidget", "Marks and matches" ) );
    visibilityBox_->setItemText( 1, QCoreApplication::translate( "CrawlerWidget", "Marks" ) );
    visibilityBox_->setItemText( 2, QCoreApplication::translate( "CrawlerWidget", "Matches" ) );
    matchCaseButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Match case" ) );
    useRegexpButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Use regex" ) );
    inverseButton_->setToolTip( QCoreApplication::translate( "CrawlerWidget", "Inverse match" ) );
    booleanButton_->setToolTip( QCoreApplication::translate(
        "CrawlerWidget", "Enable regular expression logical combining" ) );
    searchRefreshButton_->setToolTip(
        QCoreApplication::translate( "CrawlerWidget", "Auto-refresh" ) );
    clearSearchHistoryAction_->setText(
        QCoreApplication::translate( "CrawlerWidget", "Clear search history" ) );
    editSearchHistoryAction_->setText(
        QCoreApplication::translate( "CrawlerWidget", "Edit search history" ) );
    saveAsPredefinedFilterAction_->setText(
        QCoreApplication::translate( "CrawlerWidget", "Save as Filter" ) );
    searchButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Search" ) );
    clearButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Clear search text" ) );
    keepSearchResultsButton_->setText(
        QCoreApplication::translate( "CrawlerWidget", "Keep Results" ) );
    keepSearchResultsButton_->setToolTip( QCoreApplication::translate(
        "CrawlerWidget", "Keep these results and show subsequent results in a new window" ) );
    predefinedFilters_->retranslateUi();
}

void SearchPanel::loadIcons( IconLoader& icons )
{
    searchRefreshButton_->setIcon( icons.load( "icons8-search-refresh" ) );
    useRegexpButton_->setIcon( icons.load( "regex" ) );
    inverseButton_->setIcon( icons.load( "icons8-not-equal" ) );
    booleanButton_->setIcon( icons.load( "icons8-venn-diagram" ) );
    clearButton_->setIcon( icons.load( "icons8-delete" ) );
    searchButton_->setIcon( icons.load( "icons8-search" ) );
    keepSearchResultsButton_->setIcon( icons.load( "icons8-lock" ) );
    matchCaseButton_->setIcon( icons.load( "icons8-font-size" ) );
    stopButton_->setIcon( icons.load( "icons8-close-window" ) );
}

void SearchPanel::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    if ( event->type() == QEvent::LanguageChange )
        retranslateUi();
}
