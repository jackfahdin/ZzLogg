#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "persistentinfo.h"

const bool PersistentInfo::ForcePortable = false;

class SettingsOverrideTest : public QObject {
    Q_OBJECT

  private slots:
    void usesForcedIniFormatAndPath();
};

void SettingsOverrideTest::usesForcedIniFormatAndPath()
{
    QTemporaryDir settingsRoot;
    QVERIFY( settingsRoot.isValid() );
    QVERIFY2( !QFileInfo::exists( kloggPortableConfigPath() ),
              "settings override contract must run without an adjacent portable config" );

    QVERIFY( setPersistentSettingsOverrideForProcess( QSettings::IniFormat,
                                                       settingsRoot.path() ) );

    auto& appSettings = PersistentInfo::getSettings( app_settings{} );
    auto& sessionSettings = PersistentInfo::getSettings( session_settings{} );
    const QString expectedAppPath
        = QDir( settingsRoot.path() ).filePath( QStringLiteral( "klogg/klogg.ini" ) );
    const QString expectedSessionPath
        = QDir( settingsRoot.path() ).filePath( QStringLiteral( "klogg/klogg_session.ini" ) );

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

    QTemporaryDir rejectedRoot;
    QVERIFY( rejectedRoot.isValid() );
    QVERIFY( !setPersistentSettingsOverrideForProcess( QSettings::IniFormat,
                                                        rejectedRoot.path() ) );
    QCOMPARE( QDir::cleanPath( appSettings.fileName() ), QDir::cleanPath( expectedAppPath ) );
}

QTEST_APPLESS_MAIN( SettingsOverrideTest )

#include "settingsoverridetest.moc"
