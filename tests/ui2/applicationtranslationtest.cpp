#include "applicationlanguage.h"
#include "configuration.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "storagebootstrapdialog.h"
#include "storagecontext.h"
#include "storagelocationpage.h"

#include <QComboBox>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QResource>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTabWidget>
#include <QtTest>

namespace {

template <typename T>
T* child( QWidget& widget, const char* objectName )
{
    auto* result = widget.findChild<T*>( QString::fromLatin1( objectName ) );
    Q_ASSERT( result != nullptr );
    return result;
}

} // namespace

class ApplicationTranslationTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void choosesSupportedPreBootstrapLanguage_data();
    void choosesSupportedPreBootstrapLanguage();
    void translatesBootstrapUi_data();
    void translatesBootstrapUi();
    void installsEnglishWithoutBundledQtTranslator();
    void rejectsMissingApplicationTranslator();
    void retranslatesShortcutNamesAfterEnglishPrewarm();
    void retranslatesExistingOptionsDialog();
    void showsUnknownConfiguredShortcutAction();
    void changingLanguageDoesNotWriteProgramDirectory();
    void changingOnlyLanguageDoesNotRequestRestart();
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
    QCOMPARE( MainWindow::installLanguage( QStringLiteral( "missing" ) ), -1 );
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
