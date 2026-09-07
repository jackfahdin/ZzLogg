/*
 * Copyright (C) 2009, 2010, 2011, 2013 Nicolas Bonnefon and other contributors
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

// Page presentation only. Persistence and storage migration stay in optionsdialog.cpp.
#include "optionsdialog.h"
#include "encodings.h"
#include "fontutils.h"
#include "highlighteredit.h"
#include "log.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "storagelocationpage.h"
#include "styles.h"
#include <QtGui>
#include <QResource>
#include <QXmlStreamReader>
#include <QTextCodec>

void OptionsDialog::retranslateDynamicUi()
{
    const auto replaceText = []( QComboBox* box, const QVariant& itemData,
                                 const QString& text ) {
        const int index = box->findData( itemData );
        if ( index >= 0 ) {
            box->setItemText( index, text );
        }
    };

    retranslateUi( this );
    setWindowTitle( tr( "%1 preferences" ).arg( QApplication::applicationDisplayName() ) );
    tabWidget->setTabText( tabWidget->indexOf( storageLocationPage_ ), tr( "Storage" ) );
    replaceText( themeModeComboBox, int( UiThemeMode::Light ), tr( "Light" ) );
    replaceText( themeModeComboBox, int( UiThemeMode::Dark ), tr( "Dark" ) );
    replaceText( mainSearchBox, int( SearchRegexpType::ExtendedRegexp ), tr( "Extended Regexp" ) );
    replaceText( mainSearchBox, int( SearchRegexpType::FixedString ), tr( "Fixed Strings" ) );
    replaceText( quickFindSearchBox, int( SearchRegexpType::ExtendedRegexp ),
                 tr( "Extended Regexp" ) );
    replaceText( quickFindSearchBox, int( SearchRegexpType::FixedString ), tr( "Fixed Strings" ) );
    replaceText( regexpEngineComboBox, int( RegexpEngine::Hyperscan ), tr( "Hyperscan" ) );
    replaceText( regexpEngineComboBox, int( RegexpEngine::QRegularExpression ), tr( "Qt" ) );
    replaceText( encodingComboBox, -1, tr( "Auto" ) );
    retranslateShortcutTable();
}

void OptionsDialog::retranslateShortcutTable()
{
    for ( int row = 0; row < shortcutsTable->rowCount(); ++row ) {
        auto* actionItem = shortcutsTable->item( row, 0 );
        actionItem->setText( ShortcutAction::displayName(
            actionItem->data( Qt::UserRole ).toString().toStdString() ) );
    }
    if ( auto* actionHeader = shortcutsTable->horizontalHeaderItem( 0 ) ) {
        actionHeader->setText( tr( "Action" ) );
    }
    if ( auto* primaryHeader = shortcutsTable->horizontalHeaderItem( 1 ) ) {
        primaryHeader->setText( tr( "Primary shortcut" ) );
    }
    if ( auto* secondaryHeader = shortcutsTable->horizontalHeaderItem( 2 ) ) {
        secondaryHeader->setText( tr( "Secondary shortcut" ) );
    }
}

// Setups the tabs depending on the configuration
void OptionsDialog::setupTabs()
{
#ifndef Q_OS_WIN
    keepFileClosedCheckBox->setVisible( false );
#endif

#ifdef Q_OS_MAC
    minimizeToTrayCheckBox->setVisible( false );
#endif

#ifndef KLOGG_HAS_HS
    regexpEngineLabel->setVisible( false );
    regexpEngineComboBox->setVisible( false );
#endif
}

// Populates the 'family' ComboBox
void OptionsDialog::setupFontList()
{
    const auto families = FontUtils::availableFonts();
    for ( const QString& str : families ) {
        fontFamilyBox->addItem( str );
    }
}

// Populate the regexp ComboBoxes
void OptionsDialog::setupRegexp()
{
    mainSearchBox->addItem( tr( "Extended Regexp" ), int( SearchRegexpType::ExtendedRegexp ) );
    mainSearchBox->addItem( tr( "Fixed Strings" ), int( SearchRegexpType::FixedString ) );
    quickFindSearchBox->addItem( tr( "Extended Regexp" ),
                                 int( SearchRegexpType::ExtendedRegexp ) );
    quickFindSearchBox->addItem( tr( "Fixed Strings" ), int( SearchRegexpType::FixedString ) );
    regexpEngineComboBox->addItem( tr( "Hyperscan" ), int( RegexpEngine::Hyperscan ) );
    regexpEngineComboBox->addItem( tr( "Qt" ), int( RegexpEngine::QRegularExpression ) );
}

void OptionsDialog::setupStyles()
{
    styleComboBox->addItems( StyleManager::availableStyles() );
}

void OptionsDialog::setupEncodings()
{
    const auto availableEncodings = EncodingMenu::supportedEncodings();
    encodingComboBox->addItem( tr( "Auto" ), -1 );

    std::map<QString, int> allMibs;

    for ( const auto& group : availableEncodings ) {
        for ( const auto& mib : group.second ) {
            auto codec = QTextCodec::codecForMib( mib );
            if ( codec ) {
                allMibs.emplace( codec->name(), mib );
            }
        }
    }

    for ( const auto& codec : allMibs ) {
        encodingComboBox->addItem( codec.first, codec.second );
    }
}

void OptionsDialog::setupLanguageList()
{
    QResource resource( ":/i18n/Languages.xml" );
    QByteArray bytes( reinterpret_cast<const char*>( resource.data() ), (int)resource.size() );
    QXmlStreamReader xml( bytes );

    while ( !xml.atEnd() ) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if ( xml.hasError() ) {
            LOG_ERROR << "load language error";
            return;
        }

        if ( xml.name() == QString( "language" ) && token == QXmlStreamReader::StartElement ) {
            QXmlStreamAttributes attributes = xml.attributes();
            languageComboBox->addItem( attributes.value( "name" ).toString(),
                                       attributes.value( "ietfCode" ).toString() );
        }
    }
}

void OptionsDialog::setupPolling()
{
    pollIntervalLineEdit->setEnabled( pollingCheckBox->isChecked() );
}

void OptionsDialog::setupSearchResultsCache()
{
    searchCacheSpinBox->setEnabled( searchResultsCacheCheckBox->isChecked() );
}

void OptionsDialog::setupLogging()
{
    verbositySpinBox->setEnabled( loggingCheckBox->isChecked() );
}

void OptionsDialog::setupArchives()
{
    extractArchivesAlwaysCheckBox->setEnabled( extractArchivesCheckBox->isChecked() );
}

// Updates the dialog box using values in global Config()
void OptionsDialog::updateDialogFromConfig()
{
    const auto& config = Configuration::get();

    // Main font
    QFontInfo fontInfo = QFontInfo( config.mainFont() );

    int familyIndex = fontFamilyBox->findText( fontInfo.family() );
    if ( familyIndex != -1 )
        fontFamilyBox->setCurrentIndex( familyIndex );

    updateFontSize( fontInfo.family() );

    int sizeIndex = fontSizeBox->findText( QString::number( fontInfo.pointSize() ) );
    if ( sizeIndex != -1 )
        fontSizeBox->setCurrentIndex( sizeIndex );

    fontSmoothCheckBox->setChecked( config.forceFontAntialiasing() );
    boldFontCheckBox->setChecked( config.useBoldFont() );
    wrapTextCheckBox->setChecked( config.useTextWrap() );
    enableQtHiDpiCheckBox->setChecked( config.enableQtHighDpi() );
    scaleRoundingComboBox->setCurrentIndex( config.scaleFactorRounding() - 1 );

    // Language
    auto langIdx = languageComboBox->findData( { config.language() } );
    if ( langIdx == -1 ) {
        langIdx = 0;
    }
    languageComboBox->setCurrentIndex( langIdx );

    const auto style = config.style();
    if ( !styleComboBox->findText( style, Qt::MatchExactly ) ) {
        styleComboBox->setCurrentIndex( 0 );
    }
    else {
        styleComboBox->setCurrentText( style );
    }

    const auto themeIndex
        = themeModeComboBox->findData( static_cast<int>( config.uiThemeMode() ) );
    themeModeComboBox->setCurrentIndex( themeIndex < 0 ? 0 : themeIndex );

    hideAnsiColorsCheckBox->setChecked( config.hideAnsiColorSequences() );

    // Regexp types
    const int mainRegexpIndex = mainSearchBox->findData( int( config.mainRegexpType() ) );
    mainSearchBox->setCurrentIndex( mainRegexpIndex < 0 ? 0 : mainRegexpIndex );
    mainSearchColor_ = config.mainSearchBackColor();
    HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
    const int quickFindRegexpIndex = quickFindSearchBox->findData( int( config.quickfindRegexpType() ) );
    quickFindSearchBox->setCurrentIndex( quickFindRegexpIndex < 0 ? 0 : quickFindRegexpIndex );
    qfSearchColor_ = config.qfBackColor();
    HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
    const int regexpEngineIndex = regexpEngineComboBox->findData( int( config.regexpEngine() ) );
    regexpEngineComboBox->setCurrentIndex( regexpEngineIndex < 0 ? 0 : regexpEngineIndex );
    autoRunSearchOnAddCheckBox->setChecked( config.autoRunSearchOnPatternChange() );

    highlightMainSearchCheckBox->setChecked( config.mainSearchHighlight() );
    variateHighlightCheckBox->setChecked( config.variateMainSearchHighlight() );
    incrementalCheckBox->setChecked( config.isQuickfindIncremental() );
    caseSensitiveCheckBox->setChecked( !config.isSearchIgnoreCaseDefault() );
    logicalCombiningCheckBox->setChecked( config.isSearchLogicalCombiningDefault() );
    autoRefreshCheckBox->setChecked( config.isSearchAutoRefreshDefault() );

    // Polling
    nativeFileWatchCheckBox->setChecked( config.nativeFileWatchEnabled() );
    fastModificationDetectionCheckBox->setChecked( config.fastModificationDetection() );
    pollingCheckBox->setChecked( config.pollingEnabled() );
    pollIntervalLineEdit->setText( QString::number( config.pollIntervalMs() ) );
    allowFollowOnScrollCheckBox->setChecked( config.allowFollowOnScroll() );

    // Last session
    loadLastSessionCheckBox->setChecked( config.loadLastSession() );
    followFileOnLoadCheckBox->setChecked( config.followFileOnLoad() );
    minimizeToTrayCheckBox->setChecked( config.minimizeToTray() );
    multipleWindowsCheckBox->setChecked( config.allowMultipleWindows() );

    loggingCheckBox->setChecked( config.enableLogging() );
    verbositySpinBox->setValue( config.loggingLevel() );

    extractArchivesCheckBox->setChecked( config.extractArchives() );
    extractArchivesAlwaysCheckBox->setChecked( config.extractArchivesAlways() );

    // Perf
    parallelSearchCheckBox->setChecked( config.useParallelSearch() );
    searchResultsCacheCheckBox->setChecked( config.useSearchResultsCache() );
    searchCacheSpinBox->setValue( static_cast<int>( config.searchResultsCacheLines() ) );
    indexReadBufferSpinBox->setValue( config.indexReadBufferSizeMb() );
    searchReadBufferSpinBox->setValue( config.searchReadBufferSizeLines() );
    keepFileClosedCheckBox->setChecked( config.keepFileClosed() );
    compressedIndexCheckBox->setChecked( config.useCompressedIndex() );
    optimizeForNotLatinEncodingsCheckBox->setChecked( config.optimizeForNotLatinEncodings() );

    // version checking
    checkForNewVersionCheckBox->setChecked( config.versionCheckingEnabled() );

    // downloads
    verifySslCheckBox->setChecked( config.verifySslPeers() );

    const auto encodingIndex = encodingComboBox->findData( config.defaultEncodingMib() );
    encodingComboBox->setCurrentIndex( encodingIndex < 0 ? 0 : encodingIndex );

    buildShortcutsTable( false );

    const auto& savedSearches = SavedSearches::get();
    searchHistorySpinBox->setValue( savedSearches.historySize() );

    const auto& recentFiles = RecentFiles::get();
    filesHistoryMaxItemsSpinBox->setMinimum( 1 );
    filesHistoryMaxItemsSpinBox->setMaximum( MAX_RECENT_FILES );
    filesHistoryMaxItemsSpinBox->setValue( recentFiles.filesHistoryMaxItems() );
}

//
// Q_SLOTS:
//

void OptionsDialog::updateFontSize( const QString& fontFamily )
{
    QString oldFontSize = fontSizeBox->currentText();
    const auto sizes = FontUtils::availableFontSizes( fontFamily );

    fontSizeBox->clear();
    for ( int size : sizes ) {
        fontSizeBox->addItem( QString::number( size ) );
    }
    // Now restore the size we had before
    int i = fontSizeBox->findText( oldFontSize );
    if ( i != -1 )
        fontSizeBox->setCurrentIndex( i );
}

void OptionsDialog::changeMainColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( mainSearchColor_, newColor ) ) {
        mainSearchColor_ = newColor;
        HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
    }
}

void OptionsDialog::changeQfColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( qfSearchColor_, newColor ) ) {
        qfSearchColor_ = newColor;
        HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
    }
}
