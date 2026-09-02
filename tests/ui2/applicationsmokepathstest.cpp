#include "applicationsmokepaths.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class ApplicationSmokePathsTest final : public QObject {
    Q_OBJECT

  private slots:
    void acceptsOnlyPositiveSmokeDeadlines_data();
    void acceptsOnlyPositiveSmokeDeadlines();
    void recognizesOnlyManualIsolationMode();
    void showsBootstrapErrorsOnlyForInteractiveStarts();
    void invalidSmokeDeadlinesDoNotEnableOverrides_data();
    void invalidSmokeDeadlinesDoNotEnableOverrides();
    void ignoresOverridesOutsideSmokeMode();
    void acceptsAbsoluteNonRootOverridesInSmokeMode();
    void skipsProductionProviderForValidSmokeOverrides();
    void smokeRequestRemainsEnabledWhenUiRuntimeIsUnavailable();
    void leavesDefaultsWhenSmokeOverridesAreAbsent();
    void fallsBackForPartialOrUnsafeSmokeOverrides_data();
    void fallsBackForPartialOrUnsafeSmokeOverrides();
};

void ApplicationSmokePathsTest::recognizesOnlyManualIsolationMode()
{
    QVERIFY( isManualIsolationSmokeMode( QStringLiteral( "manual-isolation" ) ) );
    QVERIFY( !isManualIsolationSmokeMode( QString{} ) );
    QVERIFY( !isManualIsolationSmokeMode( QStringLiteral( "seed-session" ) ) );
}

void ApplicationSmokePathsTest::showsBootstrapErrorsOnlyForInteractiveStarts()
{
    QVERIFY( shouldShowStorageBootstrapFailureDialog( false, QString{} ) );
    QVERIFY( shouldShowStorageBootstrapFailureDialog(
        true, QStringLiteral( "manual-isolation" ) ) );
    QVERIFY( !shouldShowStorageBootstrapFailureDialog(
        true, QStringLiteral( "seed-session" ) ) );
}

void ApplicationSmokePathsTest::acceptsOnlyPositiveSmokeDeadlines_data()
{
    QTest::addColumn<QString>( "value" );
    QTest::addColumn<int>( "expectedDeadline" );

    QTest::newRow( "empty" ) << QString{} << 0;
    QTest::newRow( "not-a-number" ) << QStringLiteral( "smoke" ) << 0;
    QTest::newRow( "zero" ) << QStringLiteral( "0" ) << 0;
    QTest::newRow( "negative" ) << QStringLiteral( "-10" ) << 0;
    QTest::newRow( "positive" ) << QStringLiteral( "1800" ) << 1800;
}

void ApplicationSmokePathsTest::acceptsOnlyPositiveSmokeDeadlines()
{
    QFETCH( QString, value );
    QFETCH( int, expectedDeadline );

    QCOMPARE( validatedApplicationSmokeDeadlineMs( value ), expectedDeadline );
}

void ApplicationSmokePathsTest::invalidSmokeDeadlinesDoNotEnableOverrides_data()
{
    QTest::addColumn<QString>( "value" );

    QTest::newRow( "empty" ) << QString{};
    QTest::newRow( "not-a-number" ) << QStringLiteral( "smoke" );
    QTest::newRow( "zero" ) << QStringLiteral( "0" );
    QTest::newRow( "negative" ) << QStringLiteral( "-10" );
}

void ApplicationSmokePathsTest::invalidSmokeDeadlinesDoNotEnableOverrides()
{
    QFETCH( QString, value );
    int productionProviderCalls = 0;

    const bool smokeRequested = validatedApplicationSmokeDeadlineMs( value ) > 0;
    const auto result = resolveApplicationSmokeStoragePaths(
        smokeRequested, QStringLiteral( "D:/attempted/config" ),
        QStringLiteral( "D:/attempted/data" ), [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 1 );
    QCOMPARE( result.appConfigDirectory, QStringLiteral( "C:/production/config" ) );
    QCOMPARE( result.userDataDirectory, QStringLiteral( "C:/production/data" ) );
    QVERIFY( !result.overridden );
}

void ApplicationSmokePathsTest::ignoresOverridesOutsideSmokeMode()
{
    int productionProviderCalls = 0;
    const auto result = resolveApplicationSmokeStoragePaths(
        false, QStringLiteral( "D:/attempted/config" ), QStringLiteral( "D:/attempted/data" ),
        [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 1 );
    QCOMPARE( result.appConfigDirectory, QStringLiteral( "C:/production/config" ) );
    QCOMPARE( result.userDataDirectory, QStringLiteral( "C:/production/data" ) );
    QVERIFY( result.error.isEmpty() );
    QVERIFY( !result.overridden );
}

void ApplicationSmokePathsTest::acceptsAbsoluteNonRootOverridesInSmokeMode()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    const QString config = QDir{ root.path() }.filePath( QStringLiteral( "config" ) );
    const QString data = QDir{ root.path() }.filePath( QStringLiteral( "data" ) );
    int productionProviderCalls = 0;

    const auto result = resolveApplicationSmokeStoragePaths(
        true, config, data, [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 0 );
    QCOMPARE( result.appConfigDirectory, QDir::cleanPath( config ) );
    QCOMPARE( result.userDataDirectory, QDir::cleanPath( data ) );
    QCOMPARE( result.legacyUserSettingsDirectory,
              QDir{ QDir::cleanPath( config ) }.filePath(
                  QStringLiteral( "legacy-user-settings" ) ) );
    QVERIFY( result.error.isEmpty() );
    QVERIFY( result.overridden );
}

void ApplicationSmokePathsTest::skipsProductionProviderForValidSmokeOverrides()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    const QString config = QDir{ root.path() }.filePath( QStringLiteral( "config" ) );
    const QString data = QDir{ root.path() }.filePath( QStringLiteral( "data" ) );
    int productionProviderCalls = 0;

    const bool smokeRequested = validatedApplicationSmokeDeadlineMs( QStringLiteral( "1800" ) ) > 0;
    const auto result = resolveApplicationSmokeStoragePaths(
        smokeRequested, config, data, [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 0 );
    QCOMPARE( result.appConfigDirectory, QDir::cleanPath( config ) );
    QCOMPARE( result.userDataDirectory, QDir::cleanPath( data ) );
    QCOMPARE( result.legacyUserSettingsDirectory,
              QDir{ QDir::cleanPath( config ) }.filePath(
                  QStringLiteral( "legacy-user-settings" ) ) );
}

void ApplicationSmokePathsTest::smokeRequestRemainsEnabledWhenUiRuntimeIsUnavailable()
{
    const auto startupPlan = planApplicationSmokeStartup(
        QStringLiteral( "1800" ), QStringLiteral( "manual-isolation" ), false );

    QVERIFY( startupPlan.smokeRequest.requested );
    QCOMPARE( startupPlan.smokeRequest.deadlineMs, 1800 );
    QCOMPARE( startupPlan.smokeRequest.mode, QStringLiteral( "manual-isolation" ) );
    QVERIFY( !startupPlan.createUiRuntime );

    QTemporaryDir root;
    QVERIFY( root.isValid() );
    const QString config = QDir{ root.path() }.filePath( QStringLiteral( "config" ) );
    const QString data = QDir{ root.path() }.filePath( QStringLiteral( "data" ) );
    int productionProviderCalls = 0;

    const auto result = resolveApplicationSmokeStoragePaths(
        startupPlan.smokeRequest.requested, config, data, [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 0 );
    QVERIFY( result.overridden );
    QCOMPARE( result.legacyUserSettingsDirectory,
              QDir{ config }.filePath( QStringLiteral( "legacy-user-settings" ) ) );
}

void ApplicationSmokePathsTest::leavesDefaultsWhenSmokeOverridesAreAbsent()
{
    int productionProviderCalls = 0;
    const auto result = resolveApplicationSmokeStoragePaths(
        true, {}, {}, [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 1 );
    QCOMPARE( result.appConfigDirectory, QStringLiteral( "C:/production/config" ) );
    QCOMPARE( result.userDataDirectory, QStringLiteral( "C:/production/data" ) );
    QVERIFY( result.error.isEmpty() );
    QVERIFY( !result.overridden );
}

void ApplicationSmokePathsTest::fallsBackForPartialOrUnsafeSmokeOverrides_data()
{
    QTest::addColumn<QString>( "config" );
    QTest::addColumn<QString>( "data" );

    const QString root = QDir::rootPath();
    QTest::newRow( "config-only" ) << QStringLiteral( "C:/smoke/config" ) << QString{};
    QTest::newRow( "data-only" ) << QString{} << QStringLiteral( "C:/smoke/data" );
    QTest::newRow( "relative-config" ) << QStringLiteral( "relative/config" )
                                       << QStringLiteral( "C:/smoke/data" );
    QTest::newRow( "relative-data" ) << QStringLiteral( "C:/smoke/config" )
                                     << QStringLiteral( "relative/data" );
    QTest::newRow( "root-config" ) << root << QStringLiteral( "C:/smoke/data" );
    QTest::newRow( "root-data" ) << QStringLiteral( "C:/smoke/config" ) << root;
}

void ApplicationSmokePathsTest::fallsBackForPartialOrUnsafeSmokeOverrides()
{
    QFETCH( QString, config );
    QFETCH( QString, data );

    int productionProviderCalls = 0;
    const auto result = resolveApplicationSmokeStoragePaths(
        true, config, data, [ &productionProviderCalls ] {
            ++productionProviderCalls;
            return ApplicationSmokeStoragePaths{ QStringLiteral( "C:/production/config" ),
                                                 QStringLiteral( "C:/production/data" ),
                                                 QStringLiteral( "C:/production/legacy" ), {}, false };
        } );

    QCOMPARE( productionProviderCalls, 1 );
    QCOMPARE( result.appConfigDirectory, QStringLiteral( "C:/production/config" ) );
    QCOMPARE( result.userDataDirectory, QStringLiteral( "C:/production/data" ) );
    QVERIFY( result.error.isEmpty() );
    QVERIFY( !result.overridden );
}

QTEST_APPLESS_MAIN( ApplicationSmokePathsTest )

#include "applicationsmokepathstest.moc"
