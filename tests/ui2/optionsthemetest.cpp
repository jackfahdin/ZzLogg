#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "configuration.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "storagecontext.h"

class OptionsThemeTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void showsOnlyTheModeOwnedByTheEntryPoint()
    {
        qApp->setProperty( "zzlogg.fluentUi", false );
        OptionsDialog legacy;
        auto* legacyStyle = legacy.findChild<QGroupBox*>( QStringLiteral( "styleBox" ) );
        auto* legacyTheme = legacy.findChild<QGroupBox*>( QStringLiteral( "themeBox" ) );
        QVERIFY( legacyStyle );
        QVERIFY( legacyTheme );
        QVERIFY( !legacyStyle->isHidden() );
        QVERIFY( legacyTheme->isHidden() );

        qApp->setProperty( "zzlogg.fluentUi", true );
        OptionsDialog fluent;
        auto* fluentStyle = fluent.findChild<QGroupBox*>( QStringLiteral( "styleBox" ) );
        auto* fluentTheme = fluent.findChild<QGroupBox*>( QStringLiteral( "themeBox" ) );
        QVERIFY( fluentStyle );
        QVERIFY( fluentTheme );
        QVERIFY( fluentStyle->isHidden() );
        QVERIFY( !fluentTheme->isHidden() );
        auto* combo = fluent.findChild<QComboBox*>( QStringLiteral( "themeModeComboBox" ) );
        QVERIFY( combo );
        QCOMPARE( combo->count(), 3 );
        QCOMPARE( combo->itemData( 0 ).toInt(), static_cast<int>( UiThemeMode::System ) );
        QCOMPARE( combo->itemData( 1 ).toInt(), static_cast<int>( UiThemeMode::Light ) );
        QCOMPARE( combo->itemData( 2 ).toInt(), static_cast<int>( UiThemeMode::Dark ) );
    }

    void fluentEntrySavesThemeWithoutOverwritingStyle()
    {
        auto& config = Configuration::get();
        config.setStyle( QStringLiteral( "Fusion" ) );
        config.setUiThemeMode( UiThemeMode::System );
        qApp->setProperty( "zzlogg.fluentUi", true );
        OptionsDialog dialog;
        auto* combo = dialog.findChild<QComboBox*>( QStringLiteral( "themeModeComboBox" ) );
        QVERIFY( combo );
        combo->setCurrentIndex( 1 );
        auto* languageCombo
            = dialog.findChild<QComboBox*>( QStringLiteral( "languageComboBox" ) );
        QVERIFY( languageCombo );
        config.setLanguage( languageCombo->currentData().toString() );

        QVERIFY( QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog" ) );
        QCOMPARE( config.uiThemeMode(), UiThemeMode::Light );
        QCOMPARE( config.style(), QStringLiteral( "Fusion" ) );
    }

    void legacyEntrySavesStyleWithoutOverwritingTheme()
    {
        auto& config = Configuration::get();
        config.setUiThemeMode( UiThemeMode::Dark );
        qApp->setProperty( "zzlogg.fluentUi", false );
        OptionsDialog initialDialog;
        auto* initialCombo
            = initialDialog.findChild<QComboBox*>( QStringLiteral( "styleComboBox" ) );
        QVERIFY( initialCombo );
        const auto style = initialCombo->itemText( 0 );
        QVERIFY( !style.isEmpty() );
        config.setStyle( style );

        OptionsDialog dialog;
        auto* combo = dialog.findChild<QComboBox*>( QStringLiteral( "styleComboBox" ) );
        QVERIFY( combo );
        combo->setCurrentIndex( 0 );
        auto* languageCombo
            = dialog.findChild<QComboBox*>( QStringLiteral( "languageComboBox" ) );
        QVERIFY( languageCombo );
        config.setLanguage( languageCombo->currentData().toString() );

        QVERIFY( QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog" ) );
        QCOMPARE( config.style(), style );
        QCOMPARE( config.uiThemeMode(), UiThemeMode::Dark );
    }
};

int main( int argc, char* argv[] )
{
    QStandardPaths::setTestModeEnabled( true );
    QApplication app( argc, argv );
    app.setOrganizationName( QStringLiteral( "zzlogg-options-theme-test" ) );
    app.setApplicationName( QStringLiteral( "zzlogg-options-theme-test-20260830" ) );
    QTemporaryDir settingsDir;
    if ( !settingsDir.isValid() ) {
        return 1;
    }
    if ( !StorageContext::install(
             { StorageMode::CustomDirectory, settingsDir.path(),
               settingsDir.filePath( QStringLiteral( "storage.ini" ) ), true } ) ) {
        return 1;
    }
    Configuration::getSynced();
    SavedSearches::getSynced();
    RecentFiles::getSynced();

    OptionsThemeTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "optionsthemetest.moc"
