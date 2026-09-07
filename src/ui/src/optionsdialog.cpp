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

#include <QColorDialog>
#include <QEvent>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QUuid>
#include <QtGui>

#include "encodings.h"
#include "fontutils.h"
#include "highlighteredit.h"
#include "log.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "storagecontext.h"
#include "storagelocationpage.h"
#include "storagelocator.h"
#include "storagevalidator.h"
#include "styles.h"

#include "optionsdialog.h"

static constexpr int PollIntervalMin = 10;
static constexpr int PollIntervalMax = 3600000;

namespace {

bool samePath( const QString& left, const QString& right )
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath( QDir::fromNativeSeparators( left ) )
               .compare( QDir::cleanPath( QDir::fromNativeSeparators( right ) ), sensitivity )
           == 0;
}

bool sameLocation( const StorageLocation& left, const StorageLocation& right )
{
    return left.mode == right.mode && samePath( left.dataRoot, right.dataRoot )
           && samePath( left.locatorPath, right.locatorPath )
           && left.commandLineOverride == right.commandLineOverride;
}

bool sameRequestContents( const StorageMigrationRequest& left,
                          const StorageMigrationRequest& right )
{
    return sameLocation( left.source, right.source ) && sameLocation( left.target, right.target )
           && samePath( left.legacyConfigFile, right.legacyConfigFile )
           && samePath( left.legacySessionFile, right.legacySessionFile )
           && samePath( left.sourceLogsDirectory, right.sourceLogsDirectory )
           && samePath( left.legacyCrashDirectory, right.legacyCrashDirectory );
}

} // namespace

// Constructor
OptionsDialog::OptionsDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );
    setWindowTitle( tr( "%1 preferences" ).arg( QApplication::applicationDisplayName() ) );

    storageLocationPage_ = new StorageLocationPage{ tabWidget };
    tabWidget->addTab( storageLocationPage_, tr( "Storage" ) );
    const auto& storage = StorageContext::current();
    const StorageRuntimePaths& installedPaths = storage.runtimePaths();
    storagePaths_ = resolveOptionsDialogStoragePaths(
        { installedPaths.applicationDirectory, installedPaths.appConfigDirectory,
          installedPaths.userDataDirectory },
        { qApp->property( "zzlogg.test.applicationDirectory" ).toString(),
          qApp->property( "zzlogg.test.appConfigDirectory" ).toString(),
          qApp->property( "zzlogg.test.userDataDirectory" ).toString() },
        [] {
            return OptionsDialogStoragePaths{
                QCoreApplication::applicationDirPath(),
                QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ),
                QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) };
        } );
    storageLocationPage_->setApplicationDirectory( storagePaths_.applicationDirectory );
    storageLocationPage_->setUserDataDirectory( storagePaths_.userDataDirectory );
    storageLocationPage_->setLocation( storage.location() );
    storageLocationPage_->setCommandLineManaged( storage.location().commandLineOverride );

    const bool fluentUi = qApp->property( "zzlogg.fluentUi" ).toBool();
    styleBox->setVisible( !fluentUi );
    themeBox->setVisible( fluentUi );
    themeModeComboBox->addItem( tr( "Light" ), static_cast<int>( UiThemeMode::Light ) );
    themeModeComboBox->addItem( tr( "Dark" ), static_cast<int>( UiThemeMode::Dark ) );

    setupTabs();
    setupFontList();
    setupRegexp();
    setupStyles();
    setupEncodings();
    setupLanguageList();

    // Validators
    QValidator* pollingIntervalValidator = new QIntValidator( PollIntervalMin, PollIntervalMax, pollIntervalLineEdit );
    pollIntervalLineEdit->setValidator( pollingIntervalValidator );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &OptionsDialog::onButtonBoxClicked );
    connect( fontFamilyBox, &QComboBox::currentTextChanged, this, &OptionsDialog::updateFontSize );
    connect( pollingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupPolling(); } );
    connect( searchResultsCacheCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupSearchResultsCache(); } );
    connect( loggingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupLogging(); } );

    connect( extractArchivesCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupArchives(); } );

    connect( mainSearchColorButton, &QPushButton::clicked, this, &OptionsDialog::changeMainColor );
    connect( quickFindColorButton, &QPushButton::clicked, this, &OptionsDialog::changeQfColor );

    connect( restoreShortcutsDefaults, &QPushButton::clicked, this, [ this ]() {
        auto ret = QMessageBox::question(
            this, tr( "Restore Default Shortcuts" ), tr( "Do you want to restore default shortcuts?" ),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel );
        if ( ret == QMessageBox::Yes )
            buildShortcutsTable( true );
    } );

    updateDialogFromConfig();

    setupPolling();
    setupSearchResultsCache();
    setupLogging();
    setupArchives();
}

void OptionsDialog::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::LanguageChange ) {
        retranslateDynamicUi();
    }
    QDialog::changeEvent( event );
}

//
// Private functions
//

void OptionsDialog::checkShortcutsOnDuplicate() const
{
    static constexpr int PRIMARY_COL = 1;
    static constexpr int SECONDARY_COL = 2;

    if ( !shortcutsTable->rowCount() ) {
        return;
    }

    const auto DEFAULT_BACKGROUND = shortcutsTable->item( 0, PRIMARY_COL )->background();

    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        shortcutsTable->item( shortcutRow, PRIMARY_COL )->setBackground( DEFAULT_BACKGROUND );
        shortcutsTable->item( shortcutRow, SECONDARY_COL )->setBackground( DEFAULT_BACKGROUND );
    }

    std::unordered_map<std::string, std::pair<int, int>> uniqueShortcuts;
    bool hasDuplicateShortcuts = false;
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {

        auto hasDuplicates = [ &uniqueShortcuts, shortcutRow, this ]( int ncol ) {
            auto keySequence = static_cast<KeySequencePresenter*>(
                                   shortcutsTable->cellWidget( shortcutRow, ncol ) )
                                   ->keySequence();

            if ( !keySequence.isEmpty() ) {
                if ( auto it = uniqueShortcuts.find( keySequence.toStdString() );
                     it != uniqueShortcuts.end() ) {

                    shortcutsTable->item( it->second.first, it->second.second )
                        ->setBackground( Qt::red );
                    shortcutsTable->item( shortcutRow, ncol )->setBackground( Qt::red );

                    return true;
                }

                uniqueShortcuts.try_emplace( keySequence.toStdString(),
                                             std::make_pair( shortcutRow, ncol ) );
            }

            return false;
        };

        if ( hasDuplicates( PRIMARY_COL ) || hasDuplicates( SECONDARY_COL ) ) {
            hasDuplicateShortcuts = true;
        }
    }

    buttonBox->button( QDialogButtonBox::Ok )->setEnabled( !hasDuplicateShortcuts );
    buttonBox->button( QDialogButtonBox::Apply )->setEnabled( !hasDuplicateShortcuts );
}

bool OptionsDialog::updateConfigFromDialog()
{
    bool restartAppMessage = false;
    auto& config = Configuration::get();
    const QString requestedLanguage = languageComboBox->currentData().toString();
    if ( requestedLanguage != config.language()
         && MainWindow::installLanguage( requestedLanguage ) != 0 ) {
        const int configuredLanguageIndex = languageComboBox->findData( config.language() );
        if ( configuredLanguageIndex >= 0 ) {
            const QSignalBlocker blocker{ languageComboBox };
            languageComboBox->setCurrentIndex( configuredLanguageIndex );
        }
        retranslateDynamicUi();
        return false;
    }

    QFont font = QFont( fontFamilyBox->currentText(), ( fontSizeBox->currentText() ).toInt() );
    config.setMainFont( font );
    config.setForceFontAntialiasing( fontSmoothCheckBox->isChecked() );
    config.setUseBoldFont( boldFontCheckBox->isChecked() );
    config.setUseTextWrap( wrapTextCheckBox->isChecked() );
    config.setEnableQtHighDpi( enableQtHiDpiCheckBox->isChecked() );
    config.setScaleFactorRounding( scaleRoundingComboBox->currentIndex() + 1 );

    config.setMainRegexpType(
        static_cast<SearchRegexpType>( mainSearchBox->currentData().toInt() ) );
    config.setMainSearchBackColor( mainSearchColor_ );
    config.setEnableMainSearchHighlight( highlightMainSearchCheckBox->isChecked() );
    config.setVariateMainSearchHighlight( variateHighlightCheckBox->isChecked() );
    config.setSearchIgnoreCaseDefault( !caseSensitiveCheckBox->isChecked() );
    config.setSearchAutoRefreshDefault( autoRefreshCheckBox->isChecked() );
    config.setSearchLogicalCombiningDefault( logicalCombiningCheckBox->isChecked() );
    config.setQuickfindRegexpType(
        static_cast<SearchRegexpType>( quickFindSearchBox->currentData().toInt() ) );
    config.setQfBackColor( qfSearchColor_ );
    config.setQuickfindIncremental( incrementalCheckBox->isChecked() );
    config.setRegexpEnging(
        static_cast<RegexpEngine>( regexpEngineComboBox->currentData().toInt() ) );
    config.setAutoRunSearchOnPatternChange( autoRunSearchOnAddCheckBox->isChecked() );

    config.setNativeFileWatchEnabled( nativeFileWatchCheckBox->isChecked() );
    config.setPollingEnabled( pollingCheckBox->isChecked() );
    auto pollInterval = pollIntervalLineEdit->text().toInt();
    if ( pollInterval < PollIntervalMin )
        pollInterval = PollIntervalMin;
    else if ( pollInterval > PollIntervalMax )
        pollInterval = PollIntervalMax;

    config.setPollIntervalMs( pollInterval );
    config.setFastModificationDetection( fastModificationDetectionCheckBox->isChecked() );
    config.setAllowFollowOnScroll( allowFollowOnScrollCheckBox->isChecked() );

    config.setLoadLastSession( loadLastSessionCheckBox->isChecked() );
    config.setFollowFileOnLoad( followFileOnLoadCheckBox->isChecked() );
    config.setAllowMultipleWindows( multipleWindowsCheckBox->isChecked() );
    config.setMinimizeToTray( minimizeToTrayCheckBox->isChecked() );
    config.setEnableLogging( loggingCheckBox->isChecked() );
    config.setLoggingLevel( verbositySpinBox->value() );

    config.setExtractArchives( extractArchivesCheckBox->isChecked() );
    config.setExtractArchivesAlways( extractArchivesAlwaysCheckBox->isChecked() );

    config.setUseParallelSearch( parallelSearchCheckBox->isChecked() );
    config.setUseSearchResultsCache( searchResultsCacheCheckBox->isChecked() );
    config.setSearchResultsCacheLines( static_cast<unsigned>( searchCacheSpinBox->value() ) );
    config.setIndexReadBufferSizeMb( indexReadBufferSpinBox->value() );
    config.setSearchReadBufferSizeLines( searchReadBufferSpinBox->value() );
    config.setKeepFileClosed( keepFileClosedCheckBox->isChecked() );
    config.setUseCompressedIndex( compressedIndexCheckBox->isChecked() );
    config.setOptimizeForNotLatinEncodings( optimizeForNotLatinEncodingsCheckBox->isChecked() );

    // version checking
    config.setVersionCheckingEnabled( checkForNewVersionCheckBox->isChecked() );

    config.setVerifySslPeers( verifySslCheckBox->isChecked() );

    const bool fluentUi = qApp->property( "zzlogg.fluentUi" ).toBool();
    if ( fluentUi ) {
        config.setUiThemeMode(
            static_cast<UiThemeMode>( themeModeComboBox->currentData().toInt() ) );
    }
    else {
        restartAppMessage = config.style() != styleComboBox->currentText();
        config.setStyle( styleComboBox->currentText() );
    }
    config.setHideAnsiColorSequences( hideAnsiColorsCheckBox->isChecked() );

    config.setDefaultEncodingMib( encodingComboBox->currentData().toInt() );

    auto shortcuts = config.shortcuts();
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        QStringList actionKeys;

        auto primaryKeySequence
            = static_cast<KeySequencePresenter*>( shortcutsTable->cellWidget( shortcutRow, 1 ) )
                  ->keySequence();
        auto secondaryKeySequence
            = static_cast<KeySequencePresenter*>( shortcutsTable->cellWidget( shortcutRow, 2 ) )
                  ->keySequence();
        actionKeys << primaryKeySequence << secondaryKeySequence;

        auto action
            = shortcutsTable->item( shortcutRow, 0 )->data( Qt::UserRole ).toString().toStdString();
        shortcuts[ action ] = actionKeys;
    }
    config.setShortcuts( shortcuts );

    config.setLanguage( requestedLanguage );
    retranslateDynamicUi();

    config.save();

    auto& savedSearches = SavedSearches::get();
    savedSearches.setHistorySize( searchHistorySpinBox->value() );
    savedSearches.save();

    auto& recentFiles = RecentFiles::get();
    recentFiles.setFilesHistoryMaxItems( filesHistoryMaxItemsSpinBox->value() );
    recentFiles.save();

    auto& appSettings = PersistentInfo::getSettings( app_settings{} );
    auto& sessionSettings = PersistentInfo::getSettings( session_settings{} );
    appSettings.sync();
    sessionSettings.sync();
    if ( appSettings.status() != QSettings::NoError
         || sessionSettings.status() != QSettings::NoError ) {
        QMessageBox::critical( this, QApplication::applicationDisplayName(),
                               tr( "Failed to save settings before changing storage location." ) );
        return false;
    }

    if ( !scheduleStorageMigration() ) {
        return false;
    }

    if ( restartAppMessage ) {
        QMessageBox::warning(
            this, QApplication::applicationDisplayName(),
            QApplication::translate( "OptionsDialog",
                                     "%1 needs to be restarted to apply some changes. " )
                .arg( QApplication::applicationDisplayName() ) );
    }

    Q_EMIT optionsChanged();
    return true;
}

bool OptionsDialog::scheduleStorageMigration()
{
    const auto& storage = StorageContext::current();
    const StorageLocation source = storage.location();
    if ( source.commandLineOverride ) {
        return true;
    }
    if ( !storageLocationPage_->isSelectionValid() ) {
        QMessageBox::critical(
            this, QApplication::applicationDisplayName(),
            tr( "The selected storage location is not valid: %1" )
                .arg( storageLocationPage_->validationError() ) );
        return false;
    }

    const StorageLocatorStore locatorStore{ storagePaths_.applicationDirectory,
                                            storagePaths_.appConfigDirectory };
    StorageLocation target = storageLocationPage_->location();
    target.locatorPath = target.mode == StorageMode::ProgramDirectory
                             ? locatorStore.programLocatorPath()
                             : locatorStore.userLocatorPath();
    target.commandLineOverride = false;
    if ( sameLocation( source, target ) ) {
        return true;
    }
    if ( !samePath( source.dataRoot, target.dataRoot )
         && StorageValidator::hasCompatibleManifest( target.dataRoot ) ) {
        QMessageBox::critical(
            this, QApplication::applicationDisplayName(),
            tr( "The selected directory already contains ZzLogg data. Automatic merging is not "
                "supported; choose an empty directory." ) );
        return false;
    }

    const StorageResolution currentResolution = locatorStore.resolve();
    if ( !currentResolution.state.has_value()
         || !sameLocation( currentResolution.state->active, source ) ) {
        QMessageBox::critical(
            this, QApplication::applicationDisplayName(),
            tr( "Cannot schedule the storage change because the active locator does not match "
                "the current data directory: %1" )
                .arg( currentResolution.error ) );
        return false;
    }

    StorageMigrationRequest request;
    request.transactionId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    request.source = source;
    request.target = target;
    request.legacyConfigFile = storage.configFilePath();
    request.legacySessionFile = storage.sessionFilePath();
    request.legacyCrashDirectory = storage.crashesDirectory();
    request.sourceLogsDirectory = storage.logsDirectory();

    if ( currentResolution.state->pending.has_value() ) {
        if ( !sameRequestContents( *currentResolution.state->pending, request ) ) {
            QMessageBox::critical(
                this, QApplication::applicationDisplayName(),
                tr( "Another storage location change is already pending. Restart ZzLogg before "
                    "choosing a different location." ) );
            return false;
        }
        request = *currentResolution.state->pending;
    }
    else {
        QString error;
        if ( !locatorStore.writePending( request, &error ) ) {
            QMessageBox::critical(
                this, QApplication::applicationDisplayName(),
                tr( "Failed to schedule the storage location change: %1" ).arg( error ) );
            return false;
        }
    }

    bool restartNow = false;
    const QVariant testAnswer = qApp->property( "zzlogg.test.restartAnswer" );
    if ( testAnswer.isValid() ) {
        restartNow = testAnswer.toString() == QStringLiteral( "now" );
    }
    else {
        QMessageBox prompt{ QMessageBox::Question, QApplication::applicationDisplayName(),
                            tr( "The storage location will change after ZzLogg restarts." ),
                            QMessageBox::NoButton, this };
        auto* now = prompt.addButton( tr( "Restart now" ), QMessageBox::AcceptRole );
        prompt.addButton( tr( "Restart later" ), QMessageBox::RejectRole );
        prompt.exec();
        restartNow = prompt.clickedButton() == now;
    }
    if ( restartNow ) {
        Q_EMIT restartRequested();
    }
    return true;
}

void OptionsDialog::onButtonBoxClicked( QAbstractButton* button )
{
    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    bool applied = true;
    if ( ( role == QDialogButtonBox::AcceptRole ) || ( role == QDialogButtonBox::ApplyRole ) ) {
        applied = updateConfigFromDialog();
    }

    if ( role == QDialogButtonBox::AcceptRole && applied )
        accept();
    else if ( role == QDialogButtonBox::RejectRole )
        reject();
}

KeySequencePresenter::KeySequencePresenter( const QString& keySequence )
{
    keySequenceLabel_
        = new QLabel( QKeySequence( keySequence ).toString( QKeySequence::NativeText ) );

    auto editButton = new QPushButton();
    editButton->setText( "..." );
    editButton->setFixedWidth( 50 );

    auto layout = new QHBoxLayout();

    connect( editButton, &QPushButton::clicked, this, &KeySequencePresenter::showEditor );
    layout->addWidget( keySequenceLabel_ );
    layout->addStretch();
    layout->addWidget( editButton );
    layout->setContentsMargins( 4, 4, 4, 4 );

    this->setLayout( layout );
}

QString KeySequencePresenter::keySequence() const
{
    return keySequenceLabel_->text();
}

void KeySequencePresenter::showEditor()
{
    QDialog keyEditDialog;

    auto label = new QLabel( "Press new key combination" );
    auto editor = new QKeySequenceEdit( QKeySequence( keySequenceLabel_->text() ) );
    auto clearButton = new QToolButton();
    clearButton->setText( "Clear" );
    auto dialogButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    auto layout = new QVBoxLayout();
    layout->addWidget( label );
    auto editorLayout = new QHBoxLayout();
    editorLayout->addWidget( editor );
    editorLayout->addWidget( clearButton );
    layout->addLayout( editorLayout );
    layout->addWidget( dialogButtons );
    keyEditDialog.setLayout( layout );

    connect( clearButton, &QToolButton::clicked, editor, &QKeySequenceEdit::clear );
    connect( dialogButtons, &QDialogButtonBox::accepted, &keyEditDialog, &QDialog::accept );
    connect( dialogButtons, &QDialogButtonBox::rejected, &keyEditDialog, &QDialog::reject );

    if ( keyEditDialog.exec() == QDialog::Accepted ) {
        keySequenceLabel_->setText( editor->keySequence().toString() );
        Q_EMIT edited(); // NOTE: it's important to emit this signal only after changing
                         // \keySequenceLabel_'s text
    }
}

void OptionsDialog::buildShortcutsTable( bool useDefaultsOnly )
{
    shortcutsTable->setRowCount( 0 );

    const auto& config = Configuration::get();
    auto shortcutList = ShortcutAction::defaultShortcutList();
    if ( !useDefaultsOnly ) {
        for ( const auto& [ action, keys ] : config.shortcuts() ) {
            shortcutList[ action ].keySequence = keys;
        }
    }

    for ( const auto& [ action, shortCut ] : shortcutList ) {
        auto currentRow = shortcutsTable->rowCount();
        shortcutsTable->insertRow( currentRow );

        auto keyItem = new QTableWidgetItem( ShortcutAction::displayName( action ) );
        keyItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
        keyItem->setData( Qt::UserRole, QString::fromStdString( action ) );
        shortcutsTable->setItem( currentRow, 0, keyItem );

        auto primaryKeySequence = new KeySequencePresenter(
            shortCut.keySequence.size() > 0 ? shortCut.keySequence[ 0 ] : "" );
        shortcutsTable->setItem( currentRow, 1, new QTableWidgetItem );
        shortcutsTable->setCellWidget( currentRow, 1, primaryKeySequence );
        connect( primaryKeySequence, &KeySequencePresenter::edited, this,
                 &OptionsDialog::checkShortcutsOnDuplicate );

        auto secondaryKeySequence = new KeySequencePresenter(
            shortCut.keySequence.size() > 1 ? shortCut.keySequence[ 1 ] : "" );
        shortcutsTable->setItem( currentRow, 2, new QTableWidgetItem );
        shortcutsTable->setCellWidget( currentRow, 2, secondaryKeySequence );
        connect( secondaryKeySequence, &KeySequencePresenter::edited, this,
                 &OptionsDialog::checkShortcutsOnDuplicate );
    }

    shortcutsTable->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    shortcutsTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Interactive );
    shortcutsTable->horizontalHeader()->setMinimumSectionSize( 150 );
    shortcutsTable->resizeColumnToContents( 0 );
    shortcutsTable->setHorizontalHeaderItem( 0, new QTableWidgetItem( tr( "Action" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 1, new QTableWidgetItem( tr( "Primary shortcut" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 2,
                                             new QTableWidgetItem( tr( "Secondary shortcut" ) ) );

    // in case if user set duplicate keys and after restores defaults
    // it is need to enable back standard buttons
    checkShortcutsOnDuplicate();

    shortcutsTable->sortItems( 0 );
}
