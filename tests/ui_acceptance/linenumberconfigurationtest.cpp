#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "configuration.h"

namespace {
Configuration load( QSettings& settings )
{
    Configuration configuration;
    configuration.retrieveFromStorage( settings );
    return configuration;
}
}

class LineNumberConfigurationTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void migratesLegacyValues_data();
    void migratesLegacyValues();
    void defaultsToVisibleWhenAllKeysAreMissing();
    void newKeyTakesPriorityOverConflictingLegacyValues();
    void savingSetterValueLeavesOnlyNewKey();
};

void LineNumberConfigurationTest::migratesLegacyValues_data()
{
    QTest::addColumn<QVariant>( "main" );
    QTest::addColumn<QVariant>( "filtered" );
    QTest::addColumn<bool>( "expected" );
    QTest::newRow( "false-false" ) << QVariant::fromValue( false )
                                    << QVariant::fromValue( false ) << false;
    QTest::newRow( "false-true" ) << QVariant::fromValue( false )
                                   << QVariant::fromValue( true ) << true;
    QTest::newRow( "true-false" ) << QVariant::fromValue( true )
                                   << QVariant::fromValue( false ) << true;
    QTest::newRow( "true-true" ) << QVariant::fromValue( true )
                                  << QVariant::fromValue( true ) << true;
    QTest::newRow( "main-only-false" ) << QVariant::fromValue( false ) << QVariant{} << false;
    QTest::newRow( "main-only-true" ) << QVariant::fromValue( true ) << QVariant{} << true;
    QTest::newRow( "filtered-only-false" ) << QVariant{} << QVariant::fromValue( false ) << false;
    QTest::newRow( "filtered-only-true" ) << QVariant{} << QVariant::fromValue( true ) << true;
}

void LineNumberConfigurationTest::migratesLegacyValues()
{
    QFETCH( QVariant, main );
    QFETCH( QVariant, filtered );
    QFETCH( bool, expected );

    QTemporaryDir dir;
    QVERIFY( dir.isValid() );
    QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ), QSettings::IniFormat );
    if ( main.isValid() )
        settings.setValue( QStringLiteral( "view.lineNumbersVisibleInMain" ), main );
    if ( filtered.isValid() )
        settings.setValue( QStringLiteral( "view.lineNumbersVisibleInFiltered" ), filtered );

    const auto loaded = load( settings );
    QCOMPARE( loaded.lineNumbersVisible(), expected );
    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), expected );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInMain" ) ) );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInFiltered" ) ) );
}

void LineNumberConfigurationTest::defaultsToVisibleWhenAllKeysAreMissing()
{
    QTemporaryDir dir;
    QVERIFY( dir.isValid() );
    QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ), QSettings::IniFormat );

    const auto loaded = load( settings );
    QCOMPARE( loaded.lineNumbersVisible(), true );
    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), true );
}

void LineNumberConfigurationTest::newKeyTakesPriorityOverConflictingLegacyValues()
{
    QTemporaryDir dir;
    QVERIFY( dir.isValid() );
    QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ), QSettings::IniFormat );
    settings.setValue( QStringLiteral( "view.lineNumbersVisible" ), false );
    settings.setValue( QStringLiteral( "view.lineNumbersVisibleInMain" ), true );
    settings.setValue( QStringLiteral( "view.lineNumbersVisibleInFiltered" ), true );

    const auto loaded = load( settings );
    QCOMPARE( loaded.lineNumbersVisible(), false );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInMain" ) ) );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInFiltered" ) ) );
}

void LineNumberConfigurationTest::savingSetterValueLeavesOnlyNewKey()
{
    QTemporaryDir dir;
    QVERIFY( dir.isValid() );
    QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ), QSettings::IniFormat );
    settings.setValue( QStringLiteral( "view.lineNumbersVisibleInMain" ), true );
    settings.setValue( QStringLiteral( "view.lineNumbersVisibleInFiltered" ), false );

    Configuration configuration;
    configuration.setLineNumbersVisible( false );
    configuration.saveToStorage( settings );

    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), false );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInMain" ) ) );
    QVERIFY( !settings.contains( QStringLiteral( "view.lineNumbersVisibleInFiltered" ) ) );
}

QTEST_MAIN( LineNumberConfigurationTest )
#include "linenumberconfigurationtest.moc"
