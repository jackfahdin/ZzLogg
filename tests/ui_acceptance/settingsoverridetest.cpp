#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "persistentinfo.h"
#include "storagecontext.h"

class SettingsOverrideTest : public QObject {
    Q_OBJECT

  private slots:
    void usesInstalledStorageContext();
};

void SettingsOverrideTest::usesInstalledStorageContext()
{
    QTemporaryDir settingsRoot;
    QVERIFY( settingsRoot.isValid() );
    const StorageLocation location{ StorageMode::CustomDirectory, settingsRoot.path(),
                                    settingsRoot.filePath( QStringLiteral( "locator.ini" ) ),
                                    true };
    QVERIFY( StorageContext::install( location ) );
    QVERIFY( StorageContext::current().ensureDirectories() );

    auto& appSettings = PersistentInfo::getSettings( app_settings{} );
    auto& sessionSettings = PersistentInfo::getSettings( session_settings{} );
    const QString expectedAppPath
        = settingsRoot.filePath( QStringLiteral( "config/ZzLogg.ini" ) );
    const QString expectedSessionPath
        = settingsRoot.filePath( QStringLiteral( "session/ZzLogg_session.ini" ) );

    QCOMPARE( appSettings.format(), QSettings::IniFormat );
    QCOMPARE( sessionSettings.format(), QSettings::IniFormat );
    QCOMPARE( QDir::cleanPath( appSettings.fileName() ), QDir::cleanPath( expectedAppPath ) );
    QCOMPARE( QDir::cleanPath( sessionSettings.fileName() ),
              QDir::cleanPath( expectedSessionPath ) );

    appSettings.setValue( QStringLiteral( "contract/app" ), QStringLiteral( "written" ) );
    sessionSettings.setValue( QStringLiteral( "contract/session" ),
                              QStringLiteral( "written" ) );
    appSettings.sync();
    sessionSettings.sync();

    QCOMPARE( appSettings.status(), QSettings::NoError );
    QCOMPARE( sessionSettings.status(), QSettings::NoError );
    QVERIFY( QFileInfo::exists( expectedAppPath ) );
    QVERIFY( QFileInfo::exists( expectedSessionPath ) );
}

QTEST_APPLESS_MAIN( SettingsOverrideTest )

#include "settingsoverridetest.moc"
