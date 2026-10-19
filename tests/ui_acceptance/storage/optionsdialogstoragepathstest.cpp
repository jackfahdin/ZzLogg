#include "optionsdialogstoragepaths.h"

#include <QtTest>

class OptionsDialogStoragePathsTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void installedContextSkipsProductionProvider();
    void testOverrideSkipsProductionProvider();
    void incompleteContextUsesProductionProviderOnce();
};

void OptionsDialogStoragePathsTest::installedContextSkipsProductionProvider()
{
    int providerCalls = 0;
    const OptionsDialogStoragePaths installed{ QStringLiteral( "D:/app" ),
                                               QStringLiteral( "D:/config" ),
                                               QStringLiteral( "D:/data" ) };
    const auto resolved = resolveOptionsDialogStoragePaths(
        installed, {}, [ &providerCalls ] {
            ++providerCalls;
            return OptionsDialogStoragePaths{ QStringLiteral( "C:/real/app" ),
                                              QStringLiteral( "C:/real/config" ),
                                              QStringLiteral( "C:/real/data" ) };
        } );

    QCOMPARE( providerCalls, 0 );
    QCOMPARE( resolved.applicationDirectory, QStringLiteral( "D:/app" ) );
    QCOMPARE( resolved.appConfigDirectory, QStringLiteral( "D:/config" ) );
    QCOMPARE( resolved.userDataDirectory, QStringLiteral( "D:/data" ) );
}

void OptionsDialogStoragePathsTest::testOverrideSkipsProductionProvider()
{
    int providerCalls = 0;
    const OptionsDialogStoragePaths installed{ QStringLiteral( "D:/installed/app" ),
                                               QStringLiteral( "D:/installed/config" ),
                                               QStringLiteral( "D:/installed/data" ) };
    const OptionsDialogStoragePaths testOverride{ QStringLiteral( "D:/test/app" ),
                                                  QStringLiteral( "D:/test/config" ),
                                                  QStringLiteral( "D:/test/data" ) };
    const auto resolved = resolveOptionsDialogStoragePaths(
        installed, testOverride, [ &providerCalls ] {
            ++providerCalls;
            return OptionsDialogStoragePaths{};
        } );

    QCOMPARE( providerCalls, 0 );
    QCOMPARE( resolved.applicationDirectory, QStringLiteral( "D:/test/app" ) );
    QCOMPARE( resolved.appConfigDirectory, QStringLiteral( "D:/test/config" ) );
    QCOMPARE( resolved.userDataDirectory, QStringLiteral( "D:/test/data" ) );
}

void OptionsDialogStoragePathsTest::incompleteContextUsesProductionProviderOnce()
{
    int providerCalls = 0;
    const auto resolved = resolveOptionsDialogStoragePaths(
        { QStringLiteral( "D:/partial/app" ), {}, QStringLiteral( "D:/partial/data" ) }, {},
        [ &providerCalls ] {
            ++providerCalls;
            return OptionsDialogStoragePaths{ QStringLiteral( "C:/real/app" ),
                                              QStringLiteral( "C:/real/config" ),
                                              QStringLiteral( "C:/real/data" ) };
        } );

    QCOMPARE( providerCalls, 1 );
    QCOMPARE( resolved.applicationDirectory, QStringLiteral( "C:/real/app" ) );
    QCOMPARE( resolved.appConfigDirectory, QStringLiteral( "C:/real/config" ) );
    QCOMPARE( resolved.userDataDirectory, QStringLiteral( "C:/real/data" ) );
}

QTEST_APPLESS_MAIN( OptionsDialogStoragePathsTest )

#include "optionsdialogstoragepathstest.moc"
