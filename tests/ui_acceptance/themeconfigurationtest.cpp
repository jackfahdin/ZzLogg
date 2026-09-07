#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "configuration.h"

class ThemeConfigurationTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void readsAndWritesStableValues()
    {
        QTemporaryDir dir;
        QVERIFY( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "config.ini" ) );
        QSettings settings( path, QSettings::IniFormat );

        Configuration written;
        written.setUiThemeMode( UiThemeMode::Dark );
        written.saveToStorage( settings );
        settings.sync();
        QCOMPARE( settings.value( QStringLiteral( "view.themeMode" ) ).toString(),
                  QStringLiteral( "dark" ) );

        Configuration loaded;
        loaded.retrieveFromStorage( settings );
        QCOMPARE( loaded.uiThemeMode(), UiThemeMode::Dark );
    }

    void normalizesInvalidValueToLegacySystemForRuntimeMigration()
    {
        QTemporaryDir dir;
        QVERIFY( dir.isValid() );
        QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ),
                            QSettings::IniFormat );
        settings.setValue( QStringLiteral( "view.themeMode" ), QStringLiteral( "sepia" ) );

        Configuration loaded;
        loaded.retrieveFromStorage( settings );
        QCOMPARE( loaded.uiThemeMode(), UiThemeMode::System );
        QCOMPARE( settings.value( QStringLiteral( "view.themeMode" ) ).toString(),
                  QStringLiteral( "system" ) );
    }

    void treatsMissingValueAsLegacySystemSentinel()
    {
        QTemporaryDir dir;
        QVERIFY( dir.isValid() );
        QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ),
                            QSettings::IniFormat );
        QVERIFY( !settings.contains( QStringLiteral( "view.themeMode" ) ) );

        Configuration loaded;
        loaded.retrieveFromStorage( settings );
        QCOMPARE( loaded.uiThemeMode(), UiThemeMode::System );
    }

    void readsEveryStableValue_data()
    {
        QTest::addColumn<QString>( "storedValue" );
        QTest::addColumn<UiThemeMode>( "expectedMode" );
        QTest::newRow( "system" ) << QStringLiteral( "system" ) << UiThemeMode::System;
        QTest::newRow( "light" ) << QStringLiteral( "light" ) << UiThemeMode::Light;
        QTest::newRow( "dark" ) << QStringLiteral( "dark" ) << UiThemeMode::Dark;
    }

    void readsEveryStableValue()
    {
        QFETCH( QString, storedValue );
        QFETCH( UiThemeMode, expectedMode );
        QTemporaryDir dir;
        QVERIFY( dir.isValid() );
        QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ),
                            QSettings::IniFormat );
        settings.setValue( QStringLiteral( "view.themeMode" ), storedValue );

        Configuration loaded;
        loaded.retrieveFromStorage( settings );
        QCOMPARE( loaded.uiThemeMode(), expectedMode );
    }
};

QTEST_MAIN( ThemeConfigurationTest )
#include "themeconfigurationtest.moc"
