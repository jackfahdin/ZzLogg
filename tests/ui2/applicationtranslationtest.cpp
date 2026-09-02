#include "applicationlanguage.h"
#include "mainwindow.h"
#include "storagebootstrapdialog.h"
#include "storagelocationpage.h"

#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QResource>
#include <QTranslator>
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

QTEST_MAIN( ApplicationTranslationTest )

#include "applicationtranslationtest.moc"
