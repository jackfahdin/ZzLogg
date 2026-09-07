#include "applicationlanguage.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "documentworkspace.h"
#include "infoline.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "predefinedfilterscombobox.h"
#include "qfnotifications.h"
#include "quickfindwidget.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "session.h"
#include "shortcuts.h"
#include "storagebootstrapdialog.h"
#include "storagecontext.h"
#include "storagelocationpage.h"
#include "tabbedcrawlerwidget.h"
#include "tabbedscratchpad.h"
#include "uithemecontext.h"
#include "windowchrome.h"
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <QScopeGuard>
#include <QStyle>

#include <QAction>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QResource>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTabWidget>
#include <QtTest>

namespace {

ZzFluentUI::ZzThemeController& testTheme()
{
    static auto* theme = new ZzFluentUI::ZzThemeController(qApp);
    return *theme;
}

QMenuBar* commandMenuBar(MainWindow& window)
{
    auto* title = qobject_cast<ZzFluentUI::ZzFluentTitleBar*>(window.menuWidget());
    return title ? title->menuBar() : window.menuBar();
}

template <typename T>
T* child( QWidget& widget, const char* objectName )
{
    auto* result = widget.findChild<T*>( QString::fromLatin1( objectName ) );
    Q_ASSERT( result != nullptr );
    return result;
}

} // namespace

struct ApplicationTranslationCrawlerAccess {
};

template <>
struct CrawlerWidget::access_by<ApplicationTranslationCrawlerAccess> {
    static bool loadingFinished( const CrawlerWidget& crawler )
    {
        return !crawler.loadingInProgress_;
    }

    static void publishLoadingProgress( CrawlerWidget& crawler, int progress )
    {
        Q_EMIT crawler.logData_->loadingProgressed( progress );
    }

    static void publishActiveSearchProgress( CrawlerWidget& crawler, LinesCount matches,
                                             int progress )
    {
        crawler.searchState_.startSearch();
        crawler.stopButton_->setEnabled( true );
        crawler.stopButton_->show();
        crawler.searchButton_->hide();
        crawler.clearButton_->hide();
        Q_EMIT crawler.logFilteredData_->searchProgressed( matches, progress, 0_lnum );
    }

    static LogFilteredData* searchObject( const CrawlerWidget& crawler )
    {
        return crawler.logFilteredData_.get();
    }

    static int searchState( const CrawlerWidget& crawler )
    {
        return static_cast<int>( crawler.searchState_.getState() );
    }

    static QToolButton* stopButton( const CrawlerWidget& crawler )
    {
        return crawler.stopButton_;
    }

    static QToolButton* clearButton( const CrawlerWidget& crawler )
    {
        return crawler.clearButton_;
    }
};

using TranslationCrawlerAccess = CrawlerWidget::access_by<ApplicationTranslationCrawlerAccess>;

class ApplicationTranslationTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void choosesSupportedPreBootstrapLanguage_data();
    void choosesSupportedPreBootstrapLanguage();
    void translatesBootstrapUi_data();
    void translatesBootstrapUi();
    void installsEnglishWithoutBundledQtTranslator();
    void rejectsMissingApplicationTranslator();
    void failedLanguageSelectionRollsBackOptionsDialog();
    void retranslatesShortcutNamesAfterEnglishPrewarm();
    void retranslatesExistingOptionsDialog();
    void showsUnknownConfiguredShortcutAction();
    void changingLanguageDoesNotWriteProgramDirectory();
    void retranslatesInvalidStorageValidationWithoutWriting();
    void changingOnlyLanguageDoesNotRequestRestart();
    void retranslatesOpenDocumentSearchAndQuickFindControls();
    void retranslatesCrawlerSemanticSearchStatus();
    void resendsPerDocumentLoadingStateAfterTabSwitchAndLanguageChange();
    void opensEncodingMenuFromMenuBar();
    void rendersLayoutsAcrossThemesAndLanguages();
    void closesLastDocumentAndReopensThroughWorkspace();
    void activatesExistingDocumentInItsOwningWindow();
    void opensAndSearchesLargeLog();
    void destroyingWindowCancelsPendingVisualRefresh();
    void retranslatesExistingMainWindowChromeWithoutChangingDocumentState();
};

void ApplicationTranslationTest::choosesSupportedPreBootstrapLanguage_data()
{
    QTest::addColumn<QLocale>( "locale" );
    QTest::addColumn<QString>( "expectedLanguage" );

    QTest::newRow( "english" ) << QLocale{ QStringLiteral( "en_US" ) }
                                << QStringLiteral( "en" );
    QTest::newRow( "simplified-chinese" ) << QLocale{ QStringLiteral( "zh_CN" ) }
                                           << QStringLiteral( "zh_CN" );
    QTest::newRow( "traditional-chinese-taiwan" ) << QLocale{ QStringLiteral( "zh_TW" ) }
                                                   << QStringLiteral( "zh_TW" );
    QTest::newRow( "traditional-chinese-hong-kong" ) << QLocale{ QStringLiteral( "zh_HK" ) }
                                                      << QStringLiteral( "zh_TW" );
    QTest::newRow( "unsupported-falls-back-to-english" )
        << QLocale{ QStringLiteral( "de_DE" ) } << QStringLiteral( "en" );
}

void ApplicationTranslationTest::choosesSupportedPreBootstrapLanguage()
{
    QFETCH( QLocale, locale );
    QFETCH( QString, expectedLanguage );

    QCOMPARE( preBootstrapLanguage( locale ), expectedLanguage );
}

void ApplicationTranslationTest::translatesBootstrapUi_data()
{
    QTest::addColumn<QString>( "language" );
    QTest::addColumn<QString>( "title" );
    QTest::addColumn<QString>( "explanation" );
    QTest::addColumn<QString>( "userMode" );
    QTest::addColumn<QString>( "programMode" );
    QTest::addColumn<QString>( "customMode" );
    QTest::addColumn<QString>( "continueText" );
    QTest::addColumn<QString>( "bootstrapError" );

    QTest::newRow( "english" )
        << QStringLiteral( "en" ) << QStringLiteral( "Choose where ZzLogg stores its data" )
        << QStringLiteral( "Choose where ZzLogg stores its data." )
        << QStringLiteral( "User data directory" ) << QStringLiteral( "Program directory (data)" )
        << QStringLiteral( "Custom directory" ) << QStringLiteral( "Continue" )
        << QStringLiteral( "ZzLogg could not open its data directory:\nexample" );
    QTest::newRow( "simplified-chinese" )
        << QStringLiteral( "zh_CN" ) << QStringLiteral( "选择 ZzLogg 数据保存位置" )
        << QStringLiteral( "请选择 ZzLogg 数据的保存位置。" ) << QStringLiteral( "用户数据目录" )
        << QStringLiteral( "程序目录（data）" ) << QStringLiteral( "自定义目录" )
        << QStringLiteral( "继续" )
        << QStringLiteral( "ZzLogg 无法打开其数据目录：\nexample" );
    QTest::newRow( "traditional-chinese" )
        << QStringLiteral( "zh_TW" ) << QStringLiteral( "選擇 ZzLogg 資料儲存位置" )
        << QStringLiteral( "請選擇 ZzLogg 資料的儲存位置。" )
        << QStringLiteral( "使用者資料目錄" ) << QStringLiteral( "程式目錄（data）" )
        << QStringLiteral( "自訂目錄" ) << QStringLiteral( "繼續" )
        << QStringLiteral( "ZzLogg 無法開啟其資料目錄：\nexample" );
}

void ApplicationTranslationTest::translatesBootstrapUi()
{
    QFETCH( QString, language );
    QFETCH( QString, title );
    QFETCH( QString, explanation );
    QFETCH( QString, userMode );
    QFETCH( QString, programMode );
    QFETCH( QString, customMode );
    QFETCH( QString, continueText );
    QFETCH( QString, bootstrapError );

    QCOMPARE( MainWindow::installLanguage( language ), 0 );
    StorageBootstrapDialog dialog;
    auto* page = child<StorageLocationPage>( dialog, "storageLocationPage" );
    QCOMPARE( dialog.windowTitle(), title );
    QCOMPARE( child<QLabel>( *page, "storageLocationExplanation" )->text(), explanation );
    QCOMPARE( child<QRadioButton>( *page, "userStorageRadio" )->text(), userMode );
    QCOMPARE( child<QRadioButton>( *page, "programStorageRadio" )->text(), programMode );
    QCOMPARE( child<QRadioButton>( *page, "customStorageRadio" )->text(), customMode );
    QCOMPARE( child<QPushButton>( dialog, "storageContinueButton" )->text(), continueText );
    QCOMPARE( QApplication::translate(
                  "ApplicationRunner", "ZzLogg could not open its data directory:\n%1" )
                  .arg( QStringLiteral( "example" ) ),
              bootstrapError );
}

void ApplicationTranslationTest::installsEnglishWithoutBundledQtTranslator()
{
    QVERIFY( !QResource{ QStringLiteral( ":/i18n/qt_en.qm" ) }.isValid() );
    QResource appResource{ QStringLiteral( ":/i18n/en.qm" ) };
    QVERIFY( appResource.isValid() );
    QTranslator directTranslator;
    QVERIFY( directTranslator.load( QStringLiteral( ":/i18n/en.qm" ) ) );
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    StorageBootstrapDialog dialog;
    QCOMPARE( dialog.windowTitle(), QStringLiteral( "Choose where ZzLogg stores its data" ) );
}

void ApplicationTranslationTest::rejectsMissingApplicationTranslator()
{
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    StorageBootstrapDialog dialog;
    const QString translatedTitle = dialog.windowTitle();
    QCOMPARE( translatedTitle, QStringLiteral( "Choose where ZzLogg stores its data" ) );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "missing" ) ), -1 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( dialog.windowTitle(), translatedTitle );
    QCOMPARE( QApplication::translate( "StorageBootstrapDialog", "选择 ZzLogg 数据保存位置" ),
              translatedTitle );
}

void ApplicationTranslationTest::failedLanguageSelectionRollsBackOptionsDialog()
{
    qApp->setProperty( "zzlogg.fluentUi", true );
    auto& config = Configuration::get();
    config.setLanguage( QStringLiteral( "en" ) );
    const bool configuredBold = config.useBoldFont();
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );

    OptionsDialog dialog;
    auto* const languageCombo = child<QComboBox>( dialog, "languageComboBox" );
    auto* const boldCheckBox = child<QCheckBox>( dialog, "boldFontCheckBox" );
    auto* const themeCombo = child<QComboBox>( dialog, "themeModeComboBox" );
    int englishIndex = languageCombo->findData( QStringLiteral( "en" ) );
    if ( englishIndex < 0 ) {
        languageCombo->addItem( QStringLiteral( "English" ), QStringLiteral( "en" ) );
        englishIndex = languageCombo->count() - 1;
    }
    languageCombo->setCurrentIndex( englishIndex );
    languageCombo->addItem( QStringLiteral( "Missing" ), QStringLiteral( "missing" ) );
    languageCombo->setCurrentIndex( languageCombo->count() - 1 );
    boldCheckBox->setChecked( !configuredBold );

    bool updateSucceeded = true;
    QVERIFY( QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog", Qt::DirectConnection,
                                        Q_RETURN_ARG( bool, updateSucceeded ) ) );

    QVERIFY( !updateSucceeded );
    QCOMPARE( config.language(), QStringLiteral( "en" ) );
    QCOMPARE( config.useBoldFont(), configuredBold );
    QCOMPARE( languageCombo->currentData().toString(), QStringLiteral( "en" ) );
    QCOMPARE( themeCombo->itemText( themeCombo->findData( int( UiThemeMode::Light ) ) ),
              QStringLiteral( "Light" ) );
}

void ApplicationTranslationTest::retranslatesExistingOptionsDialog()
{
    qApp->setProperty( "zzlogg.fluentUi", true );
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    OptionsDialog dialog;
    auto* theme = child<QComboBox>( dialog, "themeModeComboBox" );
    auto* regexType = child<QComboBox>( dialog, "mainSearchBox" );
    auto* quickFindRegexType = child<QComboBox>( dialog, "quickFindSearchBox" );
    auto* regexpEngine = child<QComboBox>( dialog, "regexpEngineComboBox" );
    auto* encoding = child<QComboBox>( dialog, "encodingComboBox" );
    auto* storagePage = child<StorageLocationPage>( dialog, "storageLocationPage" );
    auto* shortcuts = child<QTableWidget>( dialog, "shortcutsTable" );
    const QVariant selectedThemeData = theme->currentData();
    const QVariant selectedRegexData = regexType->currentData();
    const QVariant selectedQuickFindRegexData = quickFindRegexType->currentData();
    const QVariant selectedEncodingData = encoding->currentData();

    QPointer<KeySequencePresenter> editedShortcut = qobject_cast<KeySequencePresenter*>(
        shortcuts->cellWidget( 0, 1 ) );
    QVERIFY( editedShortcut );
    const QString unsavedShortcut = editedShortcut->keySequence();

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( theme->itemText( theme->findData( int( UiThemeMode::Light ) ) ),
              QStringLiteral( "浅色" ) );
    QCOMPARE( theme->currentData(), selectedThemeData );
    QCOMPARE( regexType->itemText( regexType->findData( int( SearchRegexpType::ExtendedRegexp ) ) ),
              QStringLiteral( "扩展正则表达式" ) );
    QCOMPARE( regexType->currentData(), selectedRegexData );
    QCOMPARE( quickFindRegexType->itemText(
                  quickFindRegexType->findData( int( SearchRegexpType::FixedString ) ) ),
              QStringLiteral( "固定字符串" ) );
    QCOMPARE( quickFindRegexType->currentData(), selectedQuickFindRegexData );
    QCOMPARE( regexpEngine->itemText( regexpEngine->findData( int( RegexpEngine::Hyperscan ) ) ),
              QStringLiteral( "Hyperscan" ) );
    QCOMPARE( encoding->itemText( encoding->findData( -1 ) ), QStringLiteral( "自动" ) );
    QCOMPARE( encoding->currentData(), selectedEncodingData );
    QVERIFY( editedShortcut );
    QCOMPARE( editedShortcut->keySequence(), unsavedShortcut );
    QCOMPARE( shortcuts->horizontalHeaderItem( 0 )->text(), QStringLiteral( "动作" ) );
    QCOMPARE( shortcuts->horizontalHeaderItem( 1 )->text(), QStringLiteral( "首选快捷键" ) );
    QCOMPARE( shortcuts->horizontalHeaderItem( 2 )->text(), QStringLiteral( "备选快捷键" ) );
    QCOMPARE( child<QLabel>( *storagePage, "storageLocationExplanation" )->text(),
              QStringLiteral( "请选择 ZzLogg 数据的保存位置。" ) );
    QCOMPARE( child<QRadioButton>( *storagePage, "userStorageRadio" )->text(),
              QStringLiteral( "用户数据目录" ) );
    QCOMPARE( child<QLabel>( *storagePage, "dataDirectoryLabel" )->text(),
              QStringLiteral( "数据目录：" ) );
    QCOMPARE( dialog.findChild<QTabWidget*>( QStringLiteral( "tabWidget" ) )
                  ->tabText( dialog.findChild<QTabWidget*>( QStringLiteral( "tabWidget" ) )
                                 ->indexOf( storagePage ) ),
              QStringLiteral( "存储" ) );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_TW" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( theme->itemText( theme->findData( int( UiThemeMode::Light ) ) ),
              QStringLiteral( "淺色" ) );
    QCOMPARE( regexType->itemText( regexType->findData( int( SearchRegexpType::ExtendedRegexp ) ) ),
              QStringLiteral( "擴充正規表達式" ) );
    QCOMPARE( quickFindRegexType->itemText(
                  quickFindRegexType->findData( int( SearchRegexpType::FixedString ) ) ),
              QStringLiteral( "固定字串" ) );
    QCOMPARE( encoding->itemText( encoding->findData( -1 ) ), QStringLiteral( "自動" ) );
    QCOMPARE( shortcuts->horizontalHeaderItem( 0 )->text(), QStringLiteral( "動作" ) );
    QCOMPARE( child<QLabel>( *storagePage, "storageLocationExplanation" )->text(),
              QStringLiteral( "請選擇 ZzLogg 資料的儲存位置。" ) );
    QCOMPARE( child<QLabel>( *storagePage, "dataDirectoryLabel" )->text(),
              QStringLiteral( "資料目錄：" ) );
    QCOMPARE( dialog.findChild<QTabWidget*>( QStringLiteral( "tabWidget" ) )
                  ->tabText( dialog.findChild<QTabWidget*>( QStringLiteral( "tabWidget" ) )
                                 ->indexOf( storagePage ) ),
              QStringLiteral( "儲存" ) );
    QCOMPARE( theme->currentData(), selectedThemeData );
    QCOMPARE( regexType->currentData(), selectedRegexData );
    QCOMPARE( quickFindRegexType->currentData(), selectedQuickFindRegexData );
    QCOMPARE( encoding->currentData(), selectedEncodingData );
    QVERIFY( editedShortcut );
    QCOMPARE( editedShortcut->keySequence(), unsavedShortcut );
}

void ApplicationTranslationTest::retranslatesShortcutNamesAfterEnglishPrewarm()
{
    QCOMPARE( MainWindow::installLanguage( "en" ), 0 );
    const auto& definitions = ShortcutAction::defaultShortcutList();
    QVERIFY( definitions.find( ShortcutAction::MainWindowOpenFile ) != definitions.end() );
    QCOMPARE( ShortcutAction::displayName( ShortcutAction::MainWindowOpenFile ),
              QStringLiteral( "Open file" ) );

    OptionsDialog dialog;
    auto* shortcuts = child<QTableWidget>( dialog, "shortcutsTable" );
    const auto action = QString::fromLatin1( ShortcutAction::MainWindowOpenFile );
    int actionRow = -1;
    for ( int row = 0; row < shortcuts->rowCount(); ++row ) {
        if ( shortcuts->item( row, 0 )->data( Qt::UserRole ).toString() == action ) {
            actionRow = row;
            break;
        }
    }
    QVERIFY( actionRow >= 0 );

    auto* primaryShortcut
        = qobject_cast<KeySequencePresenter*>( shortcuts->cellWidget( actionRow, 1 ) );
    auto* secondaryShortcut
        = qobject_cast<KeySequencePresenter*>( shortcuts->cellWidget( actionRow, 2 ) );
    QVERIFY( primaryShortcut );
    QVERIFY( secondaryShortcut );
    const QString primaryKey = primaryShortcut->keySequence();
    const QString secondaryKey = secondaryShortcut->keySequence();

    QCOMPARE( MainWindow::installLanguage( "zh_CN" ), 0 );
    QCoreApplication::processEvents();
    QCOMPARE( ShortcutAction::displayName( ShortcutAction::MainWindowOpenFile ),
              QStringLiteral( "打开文件" ) );
    QCOMPARE( shortcuts->item( actionRow, 0 )->text(), QStringLiteral( "打开文件" ) );
    QCOMPARE( shortcuts->cellWidget( actionRow, 1 ), primaryShortcut );
    QCOMPARE( shortcuts->cellWidget( actionRow, 2 ), secondaryShortcut );
    QCOMPARE( primaryShortcut->keySequence(), primaryKey );
    QCOMPARE( secondaryShortcut->keySequence(), secondaryKey );

    QCOMPARE( MainWindow::installLanguage( "zh_TW" ), 0 );
    QCoreApplication::processEvents();
    QCOMPARE( ShortcutAction::displayName( ShortcutAction::MainWindowOpenFile ),
              QStringLiteral( "開啟檔案" ) );
    QCOMPARE( shortcuts->item( actionRow, 0 )->text(), QStringLiteral( "開啟檔案" ) );
    QCOMPARE( shortcuts->cellWidget( actionRow, 1 ), primaryShortcut );
    QCOMPARE( shortcuts->cellWidget( actionRow, 2 ), secondaryShortcut );
    QCOMPARE( primaryShortcut->keySequence(), primaryKey );
    QCOMPARE( secondaryShortcut->keySequence(), secondaryKey );
}

void ApplicationTranslationTest::showsUnknownConfiguredShortcutAction()
{
    auto& config = Configuration::get();
    const auto configuredShortcuts = config.shortcuts();
    const std::string unknownAction = "shortcut.unknown_action";
    config.setShortcuts( { { unknownAction, QStringList{ "Ctrl+Alt+U" } } } );

    OptionsDialog dialog;
    auto* shortcuts = child<QTableWidget>( dialog, "shortcutsTable" );
    int actionRow = -1;
    for ( int row = 0; row < shortcuts->rowCount(); ++row ) {
        if ( shortcuts->item( row, 0 )->data( Qt::UserRole ).toString()
             == QString::fromStdString( unknownAction ) ) {
            actionRow = row;
            break;
        }
    }
    QVERIFY( actionRow >= 0 );
    QCOMPARE( shortcuts->item( actionRow, 0 )->text(),
              QString::fromStdString( unknownAction ) );

    config.setShortcuts( configuredShortcuts );
}

void ApplicationTranslationTest::changingLanguageDoesNotWriteProgramDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString applicationDirectory = temporaryDirectory.filePath( QStringLiteral( "program" ) );
    const QString userDataDirectory = temporaryDirectory.filePath( QStringLiteral( "user-data" ) );
    QVERIFY( QDir{}.mkpath( QDir{ applicationDirectory }.filePath( QStringLiteral( "data" ) ) ) );
    QVERIFY( QDir{}.mkpath( userDataDirectory ) );

    qApp->setProperty( "zzlogg.test.applicationDirectory", applicationDirectory );
    qApp->setProperty( "zzlogg.test.appConfigDirectory",
                       temporaryDirectory.filePath( QStringLiteral( "config" ) ) );
    qApp->setProperty( "zzlogg.test.userDataDirectory", userDataDirectory );
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );

    {
        OptionsDialog dialog;
        auto* storagePage = child<StorageLocationPage>( dialog, "storageLocationPage" );
        storagePage->setLocation( { StorageMode::ProgramDirectory, {}, {}, false } );
        QVERIFY2( storagePage->isSelectionValid(), qPrintable( storagePage->validationError() ) );
        const StorageLocation selectedLocation = storagePage->location();
        const bool selectionValid = storagePage->isSelectionValid();

        QFileSystemWatcher watcher;
        QVERIFY( watcher.addPath( applicationDirectory ) );
        QSignalSpy directoryChanged{ &watcher, &QFileSystemWatcher::directoryChanged };

        QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        QTest::qWait( 200 );

        QCOMPARE( directoryChanged.count(), 0 );
        QCOMPARE( storagePage->location().mode, selectedLocation.mode );
        QCOMPARE( storagePage->location().dataRoot, selectedLocation.dataRoot );
        QCOMPARE( storagePage->isSelectionValid(), selectionValid );
    }

    qApp->setProperty( "zzlogg.test.applicationDirectory", {} );
    qApp->setProperty( "zzlogg.test.appConfigDirectory", {} );
    qApp->setProperty( "zzlogg.test.userDataDirectory", {} );
}

void ApplicationTranslationTest::retranslatesInvalidStorageValidationWithoutWriting()
{
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QTemporaryDir temporaryDirectory;
    QVERIFY( temporaryDirectory.isValid() );
    const QString invalidRoot
        = temporaryDirectory.filePath( QStringLiteral( "non-empty-storage-directory" ) );
    QVERIFY( QDir{}.mkpath( invalidRoot ) );
    const QString sentinelPath = QDir{ invalidRoot }.filePath( QStringLiteral( "user-data" ) );
    QFile file{ sentinelPath };
    QVERIFY( file.open( QIODevice::WriteOnly ) );
    QVERIFY( file.write( "user data" ) > 0 );
    file.close();

    QFileSystemWatcher watcher;
    QVERIFY( watcher.addPath( invalidRoot ) );
    QSignalSpy directoryChanged{ &watcher, &QFileSystemWatcher::directoryChanged };

    StorageLocationPage page;
    page.setLocation( { StorageMode::CustomDirectory, invalidRoot, {}, false } );
    const StorageLocation selectedLocation = page.location();
    const QString normalizedRoot = QDir::cleanPath( invalidRoot );
    QVERIFY( !page.isSelectionValid() );
    QCOMPARE( page.validationError(),
              QStringLiteral( "storage directory is not an empty managed directory: %1" )
                  .arg( normalizedRoot ) );
    QTRY_VERIFY_WITH_TIMEOUT( directoryChanged.count() > 0, 5000 );
    QCoreApplication::processEvents();
    directoryChanged.clear();

    QSignalSpy validityChanged{ &page, &StorageLocationPage::validityChanged };

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QTest::qWait( 200 );

    QCOMPARE( page.validationError(),
              QStringLiteral( "存储目录不是空的受管目录：%1" ).arg( normalizedRoot ) );
    QCOMPARE( child<QLabel>( page, "storageValidationLabel" )->text(), page.validationError() );
    QCOMPARE( page.location().mode, selectedLocation.mode );
    QCOMPARE( page.location().dataRoot, selectedLocation.dataRoot );
    QVERIFY( !page.isSelectionValid() );
    QCOMPARE( validityChanged.count(), 0 );
    QCOMPARE( directoryChanged.count(), 0 );
    QFile unchangedFile{ sentinelPath };
    QVERIFY( unchangedFile.open( QIODevice::ReadOnly ) );
    QCOMPARE( unchangedFile.readAll(), QByteArray( "user data" ) );
}

void ApplicationTranslationTest::changingOnlyLanguageDoesNotRequestRestart()
{
    qApp->setProperty( "zzlogg.fluentUi", true );
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    auto& config = Configuration::get();
    config.setLanguage( QStringLiteral( "en" ) );
    OptionsDialog dialog;
    auto* languageCombo = child<QComboBox>( dialog, "languageComboBox" );
    QSignalSpy restartSpy( &dialog, &OptionsDialog::restartRequested );
    bool restartWarningSeen = false;
    QTimer modalWatcher;
    connect( &modalWatcher, &QTimer::timeout, [&] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            restartWarningSeen = true;
            box->accept();
        }
    } );
    modalWatcher.start( 10 );

    languageCombo->setCurrentIndex( languageCombo->findData( QStringLiteral( "zh_CN" ) ) );
    QVERIFY( QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog", Qt::DirectConnection ) );
    QVERIFY( !restartWarningSeen );
    QCOMPARE( restartSpy.count(), 0 );
}

void ApplicationTranslationTest::retranslatesOpenDocumentSearchAndQuickFindControls()
{
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QTemporaryDir logs;
    QVERIFY( logs.isValid() );
    const QString logPath = logs.filePath( QStringLiteral( "runtime-translation.log" ) );
    QFile log{ logPath };
    QVERIFY( log.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QVERIFY( log.write( "matching line\nsecond matching line\n" ) > 0 );
    log.close();

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "runtime-translation" ), 0 }, UiThemeContext{testTheme()} };
    window.show();
    window.loadFileNonInteractive( logPath );
    auto* const documentTabs
        = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    QVERIFY( documentTabs );
    QTRY_COMPARE_WITH_TIMEOUT( documentTabs->count(), 1, 5000 );
    auto* const crawler = qobject_cast<CrawlerWidget*>( documentTabs->widget( 0 ) );
    QVERIFY( crawler );
    QTRY_VERIFY_WITH_TIMEOUT( TranslationCrawlerAccess::loadingFinished( *crawler ), 5000 );
    QTRY_VERIFY_WITH_TIMEOUT( crawler->isVisible(), 5000 );
    QTRY_VERIFY_WITH_TIMEOUT( crawler->findChild<QComboBox*>( QStringLiteral( "mainSearchEdit" ) )
                                  != nullptr,
                              5000 );

    auto* const searchEdit = child<QComboBox>( *crawler, "mainSearchEdit" );
    auto* const searchButton = child<QToolButton>( *crawler, "mainSearchButton" );
    auto* const clearButton = child<QToolButton>( *crawler, "clearSearchButton" );
    auto* const matchCaseButton = child<QToolButton>( *crawler, "matchCaseButton" );
    auto* const autoRefreshButton = child<QToolButton>( *crawler, "searchRefreshButton" );
    auto* const keepResultsButton = child<QToolButton>( *crawler, "keepSearchResultsButton" );
    auto* const predefinedFilters
        = child<PredefinedFiltersComboBox>( *crawler, "predefinedFilters" );
    auto* const filteredResultsTabs = child<QTabWidget>( *crawler, "filteredResultsTabs" );
    auto* const clearHistory = child<QAction>( *crawler, "clearSearchHistoryAction" );
    auto* const editHistory = child<QAction>( *crawler, "editSearchHistoryAction" );
    auto* const saveFilter = child<QAction>( *crawler, "saveAsPredefinedFilterAction" );
    auto* const searchInfo = crawler->findChild<InfoLine*>();
    auto* const quickFind = child<QuickFindWidget>( window, "quickFindWidget" );

    QVERIFY( searchEdit );
    QVERIFY( searchButton );
    QVERIFY( clearButton );
    QVERIFY( matchCaseButton );
    QVERIFY( autoRefreshButton );
    QVERIFY( keepResultsButton );
    QVERIFY( predefinedFilters );
    QVERIFY( filteredResultsTabs );
    QVERIFY( clearHistory );
    QVERIFY( editHistory );
    QVERIFY( saveFilter );
    QVERIFY( searchInfo );
    QVERIFY( quickFind );

    auto* const ignoreCase = child<QCheckBox>( *quickFind, "ignoreCaseCheckBox" );
    auto* const previous = child<QToolButton>( *quickFind, "previousButton" );
    auto* const next = child<QToolButton>( *quickFind, "nextButton" );
    auto* const quickFindEdit = child<QLineEdit>( *quickFind, "quickFindEdit" );
    auto* const notification = child<QLabel>( *quickFind, "quickFindNotification" );

    QVERIFY( ignoreCase );
    QVERIFY( previous );
    QVERIFY( next );
    QVERIFY( quickFindEdit );
    QVERIFY( notification );

    searchEdit->setCurrentText( QStringLiteral( "matching" ) );
    searchButton->click();
    QTRY_COMPARE_WITH_TIMEOUT( searchInfo->text(), QStringLiteral( "2 matches found" ), 5000 );
    QCOMPARE( filteredResultsTabs->tabText( filteredResultsTabs->currentIndex() ),
              QStringLiteral( "Find \"matching\"" ) );
    const QWidget* const existingResults = filteredResultsTabs->currentWidget();
    QSignalSpy predefinedFilterChanged{ predefinedFilters,
                                        &PredefinedFiltersComboBox::filterChanged };
    QSignalSpy predefinedModelChanged{ predefinedFilters->model(),
                                       &QAbstractItemModel::dataChanged };
    const QString searchText = searchEdit->currentText();
    const bool matchCaseChecked = matchCaseButton->isChecked();
    const bool autoRefreshChecked = autoRefreshButton->isChecked();
    const bool keepResultsChecked = keepResultsButton->isChecked();

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( searchButton->text(), QStringLiteral( "搜索" ) );
    QCOMPARE( clearButton->text(), QStringLiteral( "清除搜索文本" ) );
    QCOMPARE( keepResultsButton->text(), QStringLiteral( "保留结果" ) );
    QCOMPARE( keepResultsButton->toolTip(),
              QStringLiteral( "保留这些结果，并在新窗口中显示后续结果" ) );
    QCOMPARE( matchCaseButton->toolTip(), QStringLiteral( "匹配大小写" ) );
    QCOMPARE( predefinedFilters->itemText( 0 ), QStringLiteral( "预定义过滤器" ) );
    QCOMPARE( predefinedFilterChanged.count(), 0 );
    QVERIFY( predefinedModelChanged.count() > 0 );
    QCOMPARE( filteredResultsTabs->currentWidget(), existingResults );
    QCOMPARE( filteredResultsTabs->tabText( filteredResultsTabs->currentIndex() ),
              QStringLiteral( "查找“matching”" ) );
    QCOMPARE( ignoreCase->text(), QStringLiteral( "忽略大小写(&C)" ) );
    QCOMPARE( previous->text(), QStringLiteral( "上一个" ) );
    QCOMPARE( next->text(), QStringLiteral( "下一个" ) );
    QCOMPARE( clearHistory->text(), QStringLiteral( "清除搜索历史" ) );
    QCOMPARE( editHistory->text(), QStringLiteral( "编辑搜索历史" ) );
    QCOMPARE( saveFilter->text(), QStringLiteral( "保存为过滤器" ) );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_TW" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( child<QToolButton>( *crawler, "mainSearchButton" ), searchButton );
    QCOMPARE( child<QToolButton>( *crawler, "clearSearchButton" ), clearButton );
    QCOMPARE( child<QToolButton>( *crawler, "matchCaseButton" ), matchCaseButton );
    QCOMPARE( child<QToolButton>( *crawler, "keepSearchResultsButton" ), keepResultsButton );
    QCOMPARE( child<PredefinedFiltersComboBox>( *crawler, "predefinedFilters" ), predefinedFilters );
    QCOMPARE( child<QTabWidget>( *crawler, "filteredResultsTabs" ), filteredResultsTabs );
    QCOMPARE( child<QAction>( *crawler, "clearSearchHistoryAction" ), clearHistory );
    QCOMPARE( child<QAction>( *crawler, "editSearchHistoryAction" ), editHistory );
    QCOMPARE( child<QAction>( *crawler, "saveAsPredefinedFilterAction" ), saveFilter );
    QCOMPARE( child<QuickFindWidget>( window, "quickFindWidget" ), quickFind );
    QCOMPARE( child<QCheckBox>( *quickFind, "ignoreCaseCheckBox" ), ignoreCase );
    QCOMPARE( child<QToolButton>( *quickFind, "previousButton" ), previous );
    QCOMPARE( child<QToolButton>( *quickFind, "nextButton" ), next );
    QCOMPARE( searchEdit->currentText(), searchText );
    QCOMPARE( searchInfo->text(), QStringLiteral( "找到 2 個符合項目" ) );
    QCOMPARE( matchCaseButton->isChecked(), matchCaseChecked );
    QCOMPARE( autoRefreshButton->isChecked(), autoRefreshChecked );
    QCOMPARE( keepResultsButton->text(), QStringLiteral( "保留結果" ) );
    QCOMPARE( keepResultsButton->toolTip(),
              QStringLiteral( "保留這些結果，並在新視窗中顯示後續結果" ) );
    QCOMPARE( keepResultsButton->isChecked(), keepResultsChecked );
    QCOMPARE( predefinedFilterChanged.count(), 0 );
    QCOMPARE( filteredResultsTabs->currentWidget(), existingResults );
    QCOMPARE( filteredResultsTabs->tabText( filteredResultsTabs->currentIndex() ),
              QStringLiteral( "尋找「matching」" ) );

    quickFind->userActivate();
    quickFindEdit->setFocus();
    QTest::keyClicks( quickFindEdit, QStringLiteral( "not present" ) );
    next->click();
    QTRY_COMPARE_WITH_TIMEOUT( notification->text(),
                               QStringLiteral( "已到達檔案末尾，未找到符合項目。" ), 5000 );
}

void ApplicationTranslationTest::retranslatesCrawlerSemanticSearchStatus()
{
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QTemporaryDir logs;
    QVERIFY( logs.isValid() );
    const QString logPath = logs.filePath( QStringLiteral( "semantic-search-status.log" ) );
    QFile log{ logPath };
    QVERIFY( log.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QVERIFY( log.write( "matching line\nsecond matching line\n" ) > 0 );
    log.close();

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "semantic-search-status" ), 0 }, UiThemeContext{testTheme()} };
    window.show();
    window.loadFileNonInteractive( logPath );
    auto* const documentTabs
        = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    QVERIFY( documentTabs );
    QTRY_COMPARE_WITH_TIMEOUT( documentTabs->count(), 1, 5000 );
    auto* const crawler = qobject_cast<CrawlerWidget*>( documentTabs->currentWidget() );
    QVERIFY( crawler );
    QTRY_VERIFY_WITH_TIMEOUT( TranslationCrawlerAccess::loadingFinished( *crawler ), 5000 );
    QTRY_VERIFY_WITH_TIMEOUT( crawler->isVisible(), 5000 );

    auto* const searchEdit = child<QComboBox>( *crawler, "mainSearchEdit" );
    auto* const searchButton = child<QToolButton>( *crawler, "mainSearchButton" );
    auto* const searchInfo = crawler->findChild<InfoLine*>();
    auto* const filteredResultsTabs = child<QTabWidget>( *crawler, "filteredResultsTabs" );
    QVERIFY( searchInfo );

    QToolButton* regexpButton = nullptr;
    for ( auto* candidate : crawler->findChildren<QToolButton*>() ) {
        if ( candidate->toolTip() == QStringLiteral( "Use regex" ) ) {
            regexpButton = candidate;
            break;
        }
    }
    QVERIFY( regexpButton );
    regexpButton->setChecked( true );
    searchEdit->setCurrentText( QStringLiteral( "[" ) );
    searchButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        searchInfo->text().startsWith( QStringLiteral( "Error in expression: " ) ), 5000 );
    const QString expressionError = searchInfo->text().mid(
        QStringLiteral( "Error in expression: " ).size() );
    const QPalette errorPalette = searchInfo->palette();
    QSignalSpy loadingFinishedAfterLanguageChange{ crawler, &CrawlerWidget::loadingFinished };

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QTRY_COMPARE( searchInfo->text(),
                  QStringLiteral( "表达式错误：%1" ).arg( expressionError ) );
    QCOMPARE( searchInfo->palette(), errorPalette );
    QCOMPARE( loadingFinishedAfterLanguageChange.count(), 0 );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    regexpButton->setChecked( false );
    searchEdit->setCurrentText( QStringLiteral( "matching" ) );
    searchButton->click();
    QTRY_COMPARE_WITH_TIMEOUT( searchInfo->text(), QStringLiteral( "2 matches found" ), 5000 );
    const QWidget* const existingResults = filteredResultsTabs->currentWidget();
    LogFilteredData* const existingSearchObject
        = TranslationCrawlerAccess::searchObject( *crawler );
    const QVariant initialInterruptCount
        = existingSearchObject->property( "interruptRequestCount" );
    QVERIFY( initialInterruptCount.isValid() );
    QVERIFY( initialInterruptCount.toULongLong() > 0 );
    QSignalSpy searchProgressed{ existingSearchObject, &LogFilteredData::searchProgressed };
    TranslationCrawlerAccess::publishActiveSearchProgress( *crawler, 2_lcount, 37 );
    QTRY_COMPARE( searchInfo->text(),
                  QStringLiteral( "Search in progress (37 %)... 2 matches found so far." ) );
    const int activeSearchState = TranslationCrawlerAccess::searchState( *crawler );
    QToolButton* const stopButton = TranslationCrawlerAccess::stopButton( *crawler );
    QToolButton* const clearButton = TranslationCrawlerAccess::clearButton( *crawler );
    QVERIFY( stopButton->isVisible() );
    QVERIFY( stopButton->isEnabled() );
    QVERIFY( searchButton->isHidden() );
    QVERIFY( clearButton->isHidden() );
    QCOMPARE( searchProgressed.count(), 1 );
    searchProgressed.clear();

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_TW" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QTRY_COMPARE( searchInfo->text(),
                  QStringLiteral( "正在搜尋（37 %）... 到目前為止已找到 2 個符合項目。" ) );
    QCOMPARE( filteredResultsTabs->currentWidget(), existingResults );
    QCOMPARE( searchEdit->currentText(), QStringLiteral( "matching" ) );
    QCOMPARE( TranslationCrawlerAccess::searchObject( *crawler ), existingSearchObject );
    QCOMPARE( existingSearchObject->property( "interruptRequestCount" ).toULongLong(),
              initialInterruptCount.toULongLong() );
    QCOMPARE( TranslationCrawlerAccess::searchState( *crawler ), activeSearchState );
    QCOMPARE( TranslationCrawlerAccess::stopButton( *crawler ), stopButton );
    QCOMPARE( TranslationCrawlerAccess::clearButton( *crawler ), clearButton );
    QVERIFY( stopButton->isVisible() );
    QVERIFY( stopButton->isEnabled() );
    QVERIFY( searchButton->isHidden() );
    QVERIFY( clearButton->isHidden() );
    QCOMPARE( searchProgressed.count(), 0 );
    QCOMPARE( loadingFinishedAfterLanguageChange.count(), 0 );
}

void ApplicationTranslationTest::resendsPerDocumentLoadingStateAfterTabSwitchAndLanguageChange()
{
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QTemporaryDir logs;
    QVERIFY( logs.isValid() );
    const QString firstPath = logs.filePath( QStringLiteral( "first-loading.log" ) );
    const QString secondPath = logs.filePath( QStringLiteral( "second-loading.log" ) );
    for ( const QString& path : { firstPath, secondPath } ) {
        QFile log{ path };
        QVERIFY( log.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QVERIFY( log.write( "first line\nsecond line\n" ) > 0 );
    }

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "per-document-loading" ), 0 }, UiThemeContext{testTheme()} };
    window.show();
    window.loadFileNonInteractive( firstPath );
    window.loadFileNonInteractive( secondPath );

    auto* const documentTabs
        = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    auto* const mainInfoLine
        = window.findChild<InfoLine*>( QStringLiteral( "mainInfoLine" ) );
    QVERIFY( documentTabs );
    QVERIFY( mainInfoLine );
    QTRY_COMPARE_WITH_TIMEOUT( documentTabs->count(), 2, 5000 );
    auto* const firstCrawler = qobject_cast<CrawlerWidget*>( documentTabs->widget( 0 ) );
    auto* const secondCrawler = qobject_cast<CrawlerWidget*>( documentTabs->widget( 1 ) );
    QVERIFY( firstCrawler );
    QVERIFY( secondCrawler );
    QTRY_VERIFY_WITH_TIMEOUT( TranslationCrawlerAccess::loadingFinished( *firstCrawler ), 5000 );
    QTRY_VERIFY_WITH_TIMEOUT( TranslationCrawlerAccess::loadingFinished( *secondCrawler ), 5000 );

    documentTabs->setCurrentIndex( 0 );
    QCoreApplication::processEvents();
    QSignalSpy firstProgress{ firstCrawler, &CrawlerWidget::loadingProgressed };
    TranslationCrawlerAccess::publishLoadingProgress( *firstCrawler, 23 );
    QCOMPARE( firstProgress.count(), 1 );
    QTRY_COMPARE( mainInfoLine->text(),
                  QDir::toNativeSeparators( firstPath )
                      + QStringLiteral( " - Indexing lines... (23 %)" ) );

    documentTabs->setCurrentIndex( 1 );
    QCoreApplication::processEvents();
    QSignalSpy secondProgress{ secondCrawler, &CrawlerWidget::loadingProgressed };
    TranslationCrawlerAccess::publishLoadingProgress( *secondCrawler, 0 );
    QCOMPARE( secondProgress.count(), 1 );
    QTRY_COMPARE_WITH_TIMEOUT( mainInfoLine->text(),
                               QDir::toNativeSeparators( secondPath )
                                   + QStringLiteral( " - Indexing lines... (0 %)" ),
                               1000 );

    documentTabs->setCurrentIndex( 0 );
    QTRY_COMPARE( mainInfoLine->text(),
                  QDir::toNativeSeparators( firstPath )
                      + QStringLiteral( " - Indexing lines... (23 %)" ) );

    documentTabs->setCurrentIndex( 1 );
    QTRY_COMPARE( mainInfoLine->text(),
                  QDir::toNativeSeparators( secondPath )
                      + QStringLiteral( " - Indexing lines... (0 %)" ) );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QCOMPARE( documentTabs->currentWidget(), secondCrawler );
    QTRY_COMPARE( mainInfoLine->text(),
                  QDir::toNativeSeparators( secondPath )
                      + QStringLiteral( " - 正在索引行... (0 %)" ) );
}

void ApplicationTranslationTest::retranslatesExistingMainWindowChromeWithoutChangingDocumentState()
{
    // This catches an implementation that only retranslates controls created after the
    // translator is installed, or rebuilds the encoding menu and loses its selection.
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    QTemporaryDir logs;
    QVERIFY( logs.isValid() );
    const QString logPath = logs.filePath( QStringLiteral( "main-window-translation.log" ) );
    QFile log{ logPath };
    QVERIFY( log.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QVERIFY( log.write( "matching line\nsecond matching line\n" ) > 0 );
    log.close();

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "main-window-translation" ), 0 }, UiThemeContext{testTheme()} };
    window.show();
    window.loadFileNonInteractive( logPath );

    auto* const documentTabs
        = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    QVERIFY( documentTabs );
    QTRY_COMPARE_WITH_TIMEOUT( documentTabs->count(), 1, 5000 );
    auto* const crawler = qobject_cast<CrawlerWidget*>( documentTabs->currentWidget() );
    QVERIFY( crawler );
    QTRY_VERIFY_WITH_TIMEOUT( TranslationCrawlerAccess::loadingFinished( *crawler ), 5000 );
    QTRY_VERIFY_WITH_TIMEOUT( crawler->isVisible(), 5000 );

    auto* const searchEdit = child<QComboBox>( *crawler, "mainSearchEdit" );
    auto* const searchButton = child<QToolButton>( *crawler, "mainSearchButton" );
    auto* const searchInfo = crawler->findChild<InfoLine*>();
    QVERIFY( searchInfo );
    searchEdit->setCurrentText( QStringLiteral( "matching" ) );
    searchButton->click();
    QTRY_COMPARE_WITH_TIMEOUT( searchInfo->text(), QStringLiteral( "2 matches found" ), 5000 );

    auto* const recentFilesMenu = window.findChild<QMenu*>( QStringLiteral( "recentFilesMenu" ) );
    auto* const encodingMenu = window.findChild<QMenu*>( QStringLiteral( "encodingMenu" ) );
    auto* const trayOpenAction = window.findChild<QAction*>( QStringLiteral( "trayOpenAction" ) );
    auto* const trayQuitAction = window.findChild<QAction*>( QStringLiteral( "trayQuitAction" ) );
    auto* const showScratchPadAction
        = window.findChild<QAction*>( QStringLiteral( "showScratchPadAction" ) );
    auto* const dateField = window.findChild<QLabel*>( QStringLiteral( "dateField" ) );

    QVERIFY( recentFilesMenu );
    QVERIFY( encodingMenu );
    QVERIFY( trayOpenAction );
    QVERIFY( trayQuitAction );
    QVERIFY( showScratchPadAction );
    QVERIFY( dateField );

    auto* const encodingAutoAction
        = encodingMenu->findChild<QAction*>( QStringLiteral( "encodingAutoAction" ) );
    auto* const encodingSystemAction
        = encodingMenu->findChild<QAction*>( QStringLiteral( "encodingSystemAction" ) );
    QVERIFY( encodingAutoAction );
    QVERIFY( encodingSystemAction );

    QCOMPARE( recentFilesMenu->title(), QStringLiteral( "Open Recent" ) );
    QCOMPARE( QString{ encodingMenu->title() }.remove( '&' ), QStringLiteral( "Encoding" ) );
    QCOMPARE( encodingAutoAction->text(), QStringLiteral( "Auto" ) );
    QCOMPARE( encodingAutoAction->statusTip(),
              QStringLiteral( "Automatically detect the file's encoding" ) );
    QVERIFY( encodingSystemAction->text().startsWith( QStringLiteral( "System (" ) ) );
    QCOMPARE( trayOpenAction->text(), QStringLiteral( "Open window" ) );
    QCOMPARE( trayQuitAction->text(), QStringLiteral( "Quit" ) );
    QVERIFY( dateField->text().startsWith( QStringLiteral( "modified on " ) ) );

    showScratchPadAction->trigger();
    TabbedScratchPad* scratchPad = nullptr;
    for ( QWidget* topLevelWidget : QApplication::topLevelWidgets() ) {
        if ( auto* candidate = qobject_cast<TabbedScratchPad*>( topLevelWidget ) ) {
            scratchPad = candidate;
            break;
        }
    }
    QVERIFY( scratchPad );

    const int currentTab = documentTabs->currentIndex();
    const QWidget* const currentDocument = documentTabs->currentWidget();
    const QString currentSearchText = searchEdit->currentText();
    const QString currentTitle = window.windowTitle();
    const bool autoEncodingChecked = encodingAutoAction->isChecked();

    QLabel* encodingField = nullptr;
    QLabel* lineNumberField = nullptr;
    for ( auto* candidate : window.findChildren<QLabel*>() ) {
        if ( candidate->text().startsWith( QStringLiteral( "Detected as " ) ) ) {
            encodingField = candidate;
        }
    }
    QVERIFY( encodingField );
    const QString encodingName
        = encodingField->text().mid( QStringLiteral( "Detected as " ).size() );
    QVERIFY( QMetaObject::invokeMethod(
        &window, "lineNumberHandler", Qt::DirectConnection,
        Q_ARG( LineNumber, LineNumber( 0 ) ), Q_ARG( LinesCount, LinesCount( 1 ) ),
        Q_ARG( LineColumn, LineColumn( 3 ) ), Q_ARG( LineLength, LineLength( 4 ) ) ) );
    for ( auto* candidate : window.findChildren<QLabel*>() ) {
        if ( candidate->text() == QStringLiteral( "Ln:1/2 Col:3 Sel:4|1" ) ) {
            lineNumberField = candidate;
            break;
        }
    }
    QVERIFY( lineNumberField );

    InfoLine* mainInfoLine = nullptr;
    const QString nativeLogPath = QDir::toNativeSeparators( logPath );
    for ( auto* candidate : window.findChildren<InfoLine*>() ) {
        if ( candidate->text() == nativeLogPath ) {
            mainInfoLine = candidate;
            break;
        }
    }
    QVERIFY( mainInfoLine );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( recentFilesMenu->title(), QStringLiteral( "最近打开文件" ) );
    // The existing Chinese menu translation retains a mnemonic suffix ("(&E)").
    QVERIFY( encodingMenu->title().startsWith( QStringLiteral( "编码" ) ) );
    QCOMPARE( encodingAutoAction->text(), QStringLiteral( "自动" ) );
    QCOMPARE( encodingAutoAction->statusTip(), QStringLiteral( "自动检测文件的编码" ) );
    QVERIFY( encodingSystemAction->text().startsWith( QStringLiteral( "跟随系统（" ) ) );
    QCOMPARE( trayOpenAction->text(), QStringLiteral( "打开窗口" ) );
    QCOMPARE( trayQuitAction->text(), QStringLiteral( "退出" ) );
    QCOMPARE( scratchPad->windowTitle(), QStringLiteral( "ZzLogg - 暂存器" ) );
    QVERIFY( window.windowTitle().contains( QStringLiteral( "ZzLogg" ) ) );
    QVERIFY( dateField->text().startsWith( QStringLiteral( "修改于 " ) ) );
    QCOMPARE( documentTabs->currentIndex(), currentTab );
    QCOMPARE( documentTabs->currentWidget(), currentDocument );
    QCOMPARE( searchEdit->currentText(), currentSearchText );
    QCOMPARE( searchInfo->text(), QStringLiteral( "找到2个匹配" ) );
    QCOMPARE( encodingAutoAction->isChecked(), autoEncodingChecked );
    QTRY_COMPARE( encodingField->text(), QStringLiteral( "检测到编码: %1" ).arg( encodingName ) );
    QTRY_COMPARE( lineNumberField->text(), QStringLiteral( "行：1/2 列：3 选择：4|1" ) );

    QVERIFY( QMetaObject::invokeMethod(
        &window, "lineNumberHandler", Qt::DirectConnection,
        Q_ARG( LineNumber, LineNumber( 0 ) ), Q_ARG( LinesCount, LinesCount( 2 ) ),
        Q_ARG( LineColumn, LineColumn( 0 ) ), Q_ARG( LineLength, LineLength( 4 ) ) ) );
    QCOMPARE( lineNumberField->text(), QStringLiteral( "行：1/2 选择：4|2" ) );

    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_TW" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    QCOMPARE( child<QMenu>( window, "recentFilesMenu" ), recentFilesMenu );
    QCOMPARE( child<QMenu>( window, "encodingMenu" ), encodingMenu );
    QCOMPARE( child<QAction>( *encodingMenu, "encodingAutoAction" ), encodingAutoAction );
    QCOMPARE( child<QAction>( *encodingMenu, "encodingSystemAction" ), encodingSystemAction );
    QCOMPARE( child<QAction>( window, "trayOpenAction" ), trayOpenAction );
    QCOMPARE( child<QAction>( window, "trayQuitAction" ), trayQuitAction );
    QVERIFY( encodingMenu->title().startsWith( QStringLiteral( "編碼" ) ) );
    QCOMPARE( scratchPad->windowTitle(), QStringLiteral( "ZzLogg - 便條" ) );
    QCOMPARE( documentTabs->currentIndex(), currentTab );
    QCOMPARE( documentTabs->currentWidget(), currentDocument );
    QCOMPARE( searchEdit->currentText(), currentSearchText );
    QCOMPARE( searchInfo->text(), QStringLiteral( "找到 2 個符合項目" ) );
    QCOMPARE( encodingAutoAction->isChecked(), autoEncodingChecked );
    QCOMPARE( encodingAutoAction->statusTip(), QStringLiteral( "自動偵測檔案的編碼" ) );
    QTRY_COMPARE( encodingField->text(), QStringLiteral( "偵測為 %1" ).arg( encodingName ) );
    QTRY_COMPARE( lineNumberField->text(), QStringLiteral( "行數：1/2 選取：4|2" ) );
    QCOMPARE( mainInfoLine->text(), nativeLogPath );
    QVERIFY( window.windowTitle().contains( QFileInfo{ logPath }.fileName() ) );
    QCOMPARE( window.windowTitle(), currentTitle );

    QVERIFY( QMetaObject::invokeMethod( &window, "updateLoadingProgress", Qt::DirectConnection,
                                        Q_ARG( int, 37 ) ) );
    QCOMPARE( mainInfoLine->text(),
              nativeLogPath + QStringLiteral( " - 正在建立行數索引... (37 %)" ) );
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "zh_CN" ) ), 0 );
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QTRY_COMPARE( mainInfoLine->text(),
                  nativeLogPath + QStringLiteral( " - 正在索引行... (37 %)" ) );
    QCOMPARE( documentTabs->currentWidget(), currentDocument );
    QCOMPARE( searchEdit->currentText(), currentSearchText );
}

void ApplicationTranslationTest::destroyingWindowCancelsPendingVisualRefresh()
{
    // Closing a window must cancel its queued style/icon work, not leave it for the next window.
    auto session = std::make_shared<Session>();
    auto* window = new MainWindow(
        WindowSession{ session, QStringLiteral( "pending-visual-refresh" ), 0 } );
    QEvent styleChange{ QEvent::StyleChange };
    QCoreApplication::sendEvent( window, &styleChange );
    delete window;
    bool queueDrained = false;
    QMetaObject::invokeMethod( qApp, [ &queueDrained ] { queueDrained = true; },
                               Qt::QueuedConnection );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::MetaCall );
    QVERIFY( queueDrained );
}

void ApplicationTranslationTest::opensEncodingMenuFromMenuBar()
{
    // This catches reparenting the encoding QMenu as a regular child widget, which exposes its
    // checked "Auto" action in the window and prevents the top-level menu from opening.
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "en" ) ), 0 );
    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "encoding-menu-popup" ), 0 }, UiThemeContext{testTheme()} };
    window.resize( 1600, 900 );
    window.show();
    QTRY_VERIFY( window.isVisible() );

    auto* const encodingMenu = child<QMenu>( window, "encodingMenu" );
    auto* const encodingAutoAction
        = child<QAction>( *encodingMenu, "encodingAutoAction" );
    QMenuBar* const mainMenuBar = commandMenuBar(window);
    QVERIFY(qobject_cast<ZzFluentUI::ZzFluentTitleBar*>(window.menuWidget()));
    QAction* const encodingMenuAction = encodingMenu->menuAction();

    QVERIFY( mainMenuBar->actions().contains( encodingMenuAction ) );
    QVERIFY( !mainMenuBar->actions().contains( encodingAutoAction ) );
    QVERIFY( encodingMenu->actions().contains( encodingAutoAction ) );
    QVERIFY( !encodingMenu->isVisible() );

    const QRect encodingActionGeometry = mainMenuBar->actionGeometry( encodingMenuAction );
    QVERIFY( encodingActionGeometry.isValid() );
    QTest::mouseClick( mainMenuBar, Qt::LeftButton, Qt::NoModifier,
                       encodingActionGeometry.center() );

    QTRY_VERIFY( encodingMenu->isVisible() );
    QVERIFY( encodingMenu->isWindow() );
    QCOMPARE( encodingMenu->windowType(), Qt::Popup );
    encodingMenu->hide();
}

void ApplicationTranslationTest::closesLastDocumentAndReopensThroughWorkspace()
{
    QCOMPARE(MainWindow::installLanguage(QStringLiteral("en")), 0);
    QTemporaryDir logs;
    const QString path = logs.filePath("workspace.log");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("one line\n") > 0);
    file.close();
    auto session = std::make_shared<Session>();
    MainWindow window(WindowSession(session, "workspace-integration", 0), {testTheme()});
    window.show();
    auto* workspace = window.findChild<DocumentWorkspace*>("documentTabs");
    QVERIFY(workspace);
    window.loadFileNonInteractive(path);
    QTRY_COMPARE(workspace->count(), 1);
    QPointer<CrawlerWidget> document = workspace->currentDocument();
    QVERIFY(document);
    QTRY_VERIFY_WITH_TIMEOUT(TranslationCrawlerAccess::loadingFinished(*document), 5000);
    Q_EMIT workspace->tabCloseRequested(0);
    QCOMPARE(workspace->count(), 0);
    QVERIFY(!session->getViewIfOpen(path));
    QCOMPARE(window.windowTitle(), QStringLiteral("ZzLogg"));
    auto* edit = commandMenuBar(window)->actions().at(1)->menu();
    QVERIFY(edit);
    QVERIFY(!edit->isEnabled());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(document.isNull());
    window.loadFileNonInteractive(path);
    QTRY_COMPARE(workspace->count(), 1);
    QVERIFY(edit->isEnabled());
    QVERIFY(window.windowTitle().contains("workspace.log"));
}

void ApplicationTranslationTest::activatesExistingDocumentInItsOwningWindow()
{
    QTemporaryDir logs;
    const QString path = logs.filePath("shared.log");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("shared log\n") > 0);
    file.close();
    auto session = std::make_shared<Session>();
    MainWindow first(WindowSession(session, "workspace-owner", 0), {testTheme()});
    MainWindow second(WindowSession(session, "workspace-other", 1), {testTheme()});
    auto* owner = first.findChild<DocumentWorkspace*>("documentTabs");
    auto* other = second.findChild<DocumentWorkspace*>("documentTabs");
    QVERIFY(owner);
    QVERIFY(other);
    first.loadFileNonInteractive(path);
    QTRY_COMPARE(owner->count(), 1);
    auto* original = owner->currentDocument();
    const QString otherPath = logs.filePath("other.log");
    QFile otherFile(otherPath);
    QVERIFY(otherFile.open(QIODevice::WriteOnly));
    QVERIFY(otherFile.write("another document\n") > 0);
    otherFile.close();
    first.loadFileNonInteractive(otherPath);
    QTRY_COMPARE(owner->count(), 2);
    QVERIFY(owner->currentDocument() != original);
    second.loadFileNonInteractive(path);
    QCOMPARE(owner->count(), 2);
    QCOMPARE(other->count(), 0);
    QCOMPARE(owner->currentDocument(), original);
    QCOMPARE(session->getViewIfOpen(path), static_cast<ViewInterface*>(original));
}

void ApplicationTranslationTest::rendersLayoutsAcrossThemesAndLanguages()
{
    // Exercise the real widgets/style, not a mockup, and retain images for visual inspection.
    auto* previousStyle = QApplication::style();
    const auto previousPalette = QApplication::palette();
    const auto previousFluent = qApp->property("zzlogg.fluentUi");
    previousStyle->setParent(nullptr);
    auto restore = qScopeGuard([&] {
        QApplication::setStyle(previousStyle);
        QApplication::setPalette(previousPalette);
        qApp->setProperty("zzlogg.fluentUi", previousFluent);
    });
    auto& theme = testTheme();
    qApp->setProperty("zzlogg.fluentUi", true);
    QApplication::setStyle(new ZzFluentUI::ZzFluentStyle(&theme));
    QTemporaryDir logs;
    QVERIFY(logs.isValid());
    const auto path = logs.filePath("example.log");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("2026-09-07 INFO Application started\n2026-09-07 WARN Retry connection\n2026-09-07 ERROR Connection refused\n");
    file.close();
    const QString captureDir = QDir(QStringLiteral(ZZLOGG_UI_CAPTURE_DIR))
        .filePath(QGuiApplication::platformName());
    QVERIFY(QDir().mkpath(captureDir));
    for (auto mode : {ZzFluentUI::ZzThemeMode::Light, ZzFluentUI::ZzThemeMode::Dark}) {
        theme.setMode(mode);
        for (const QString& language : {QStringLiteral("en"), QStringLiteral("zh_CN")}) {
            QCOMPARE(MainWindow::installLanguage(language), 0);
            auto session = std::make_shared<Session>();
            MainWindow window(WindowSession(session, "layout-capture", 0), {theme});
            window.loadFileNonInteractive(path);
            window.show();
            auto* tabs = window.findChild<TabbedCrawlerWidget*>("documentTabs");
            QVERIFY(tabs);
            QTRY_COMPARE(tabs->count(), 1);
            auto* crawler = qobject_cast<CrawlerWidget*>(tabs->currentWidget());
            QVERIFY(crawler);
            QTRY_VERIFY_WITH_TIMEOUT(TranslationCrawlerAccess::loadingFinished(*crawler), 5000);
            for (int width : {1400, 800}) {
                window.resize(width, 780);
                QTest::qWait(50);
                auto* title = qobject_cast<ZzFluentUI::ZzFluentTitleBar*>(window.menuWidget());
                QVERIFY(title);
                QVERIFY(title->isVisible());
                QVERIFY(window.centralWidget()->isVisible());
                QCOMPARE(title->title(), window.windowTitle());
                const QString name = QString("%1-%2-%3.png")
                    .arg(mode == ZzFluentUI::ZzThemeMode::Dark ? "dark" : "light", language)
                    .arg(width);
                QVERIFY(window.grab().save(QDir(captureDir).filePath(name)));
            }
        }
    }
}

void ApplicationTranslationTest::opensAndSearchesLargeLog()
{
    QCOMPARE(MainWindow::installLanguage(QStringLiteral("en")), 0);
    QTemporaryDir logs;
    QVERIFY(logs.isValid());
    const QString path = logs.filePath("large.log");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block = QByteArray("2026-09-07 ERROR connection refused request=1234567890 service=sample\n")
        + QByteArray("2026-09-07 INFO normal response request=1234567890 service=sample\n").repeated(1023);
    for (int i = 0; i < 2048; ++i)
        QCOMPARE(file.write(block), qint64(block.size()));
    const qint64 bytes = file.size();
    file.close();
    auto session = std::make_shared<Session>();
    MainWindow window(WindowSession(session, "large-log", 0), UiThemeContext{testTheme()});
    window.show();
    QElapsedTimer timer;
    timer.start();
    window.loadFileNonInteractive(path);
    auto* tabs = window.findChild<TabbedCrawlerWidget*>("documentTabs");
    QVERIFY(tabs);
    QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 1, 60000);
    auto* crawler = qobject_cast<CrawlerWidget*>(tabs->currentWidget());
    QVERIFY(crawler);
    QTRY_VERIFY_WITH_TIMEOUT(TranslationCrawlerAccess::loadingFinished(*crawler), 60000);
    const auto openMs = timer.elapsed();
    auto* edit = crawler->findChild<QComboBox*>("mainSearchEdit");
    auto* button = crawler->findChild<QToolButton*>("mainSearchButton");
    auto* info = crawler->findChild<InfoLine*>();
    QVERIFY(edit);
    QVERIFY(button);
    QVERIFY(info);
    edit->setCurrentText("ERROR");
    timer.restart();
    button->click();
    QTRY_COMPARE_WITH_TIMEOUT(info->text(), QStringLiteral("2048 matches found"), 60000);
    qInfo("Large log: bytes=%lld, lines=2097152, open_ms=%lld, search_ms=%lld",
          static_cast<long long>(bytes), static_cast<long long>(openMs),
          static_cast<long long>(timer.elapsed()));
}

int main( int argc, char* argv[] )
{
    QStandardPaths::setTestModeEnabled( true );
    QApplication app( argc, argv );
    app.setOrganizationName( QStringLiteral( "zzlogg-application-translation-test" ) );
    app.setApplicationName( QStringLiteral( "zzlogg-application-translation-test-20260903" ) );
    QTemporaryDir settingsDirectory;
    QString storageError;
    if ( !settingsDirectory.isValid()
         || !StorageContext::install( { StorageMode::CustomDirectory, settingsDirectory.path(),
                                        settingsDirectory.filePath( QStringLiteral( "storage.ini" ) ),
                                        true },
                                      &storageError ) ) {
        qCritical().noquote() << storageError;
        return 1;
    }
    Configuration::getSynced();
    SavedSearches::getSynced();
    RecentFiles::getSynced();

    ApplicationTranslationTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "applicationtranslationtest.moc"
