#include "configuration.h"
#include "optionsdialog.h"
#include "persistentinfo.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "storagecontext.h"
#include "storagelocationpage.h"
#include "storagelocator.h"
#include "storagevalidator.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

int fail( const QString& message )
{
    qCritical().noquote() << message;
    return 1;
}

bool samePath( const QString& left, const QString& right )
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath( QDir::fromNativeSeparators( left ) )
               .compare( QDir::cleanPath( QDir::fromNativeSeparators( right ) ), sensitivity )
           == 0;
}

void closeMessageBoxesSoon()
{
    QTimer::singleShot( 0, [] {
        for ( QWidget* widget : QApplication::topLevelWidgets() ) {
            if ( auto* box = qobject_cast<QMessageBox*>( widget ) ) {
                box->accept();
            }
        }
    } );
}

int runChildScenario( const QString& scenario, const QString& root )
{
    const QString appDirectory = QDir{ root }.filePath( QStringLiteral( "app" ) );
    const QString appConfigDirectory = QDir{ root }.filePath( QStringLiteral( "app-config" ) );
    const QString userDataDirectory = QDir{ root }.filePath( QStringLiteral( "user-data" ) );
    const StorageLocatorStore store{ appDirectory, appConfigDirectory };
    const bool commandLine = scenario == QStringLiteral( "command-line" );
    const bool sameRootMode = scenario == QStringLiteral( "same-root-mode" );
    const bool installedRuntimePaths
        = scenario == QStringLiteral( "installed-runtime-paths" );
    const QString sourceRoot
        = sameRootMode ? QDir{ appDirectory }.filePath( QStringLiteral( "data" ) )
                       : QDir{ root }.filePath( QStringLiteral( "source" ) );
    const StorageLocation source{
        StorageMode::CustomDirectory, sourceRoot,
        commandLine ? QString{} : store.userLocatorPath(), commandLine
    };

    QString error;
    if ( !commandLine && !store.writeActive( source, &error ) ) {
        return fail( QStringLiteral( "failed to create isolated active locator: %1" ).arg( error ) );
    }
    const bool installed = installedRuntimePaths
                               ? StorageContext::install(
                                   source,
                                   { appDirectory, appConfigDirectory, userDataDirectory },
                                   &error )
                               : StorageContext::install( source, &error );
    if ( !installed ) {
        return fail( QStringLiteral( "failed to install isolated context: %1" ).arg( error ) );
    }
    if ( !StorageContext::current().ensureDirectories( &error ) ) {
        return fail( QStringLiteral( "failed to create isolated storage: %1" ).arg( error ) );
    }
    if ( !StorageValidator::writeManifest( StorageContext::current(), &error ) ) {
        return fail( QStringLiteral( "failed to mark isolated storage managed: %1" ).arg( error ) );
    }

    if ( !installedRuntimePaths ) {
        qApp->setProperty( "zzlogg.test.applicationDirectory", appDirectory );
        qApp->setProperty( "zzlogg.test.appConfigDirectory", appConfigDirectory );
        qApp->setProperty( "zzlogg.test.userDataDirectory", userDataDirectory );
    }
    qApp->setProperty( "zzlogg.test.restartAnswer",
                       scenario == QStringLiteral( "later" ) ? QStringLiteral( "later" )
                                                               : QStringLiteral( "now" ) );

    Configuration::getSynced();
    SavedSearches::getSynced();
    RecentFiles::getSynced();

    OptionsDialog dialog;
    auto* page = dialog.findChild<StorageLocationPage*>( QStringLiteral( "storageLocationPage" ) );
    if ( page == nullptr ) {
        return fail( QStringLiteral( "Preferences has no storageLocationPage" ) );
    }
    if ( !page->isSelectionValid() ) {
        return fail( QStringLiteral( "current managed storage is invalid: %1" )
                         .arg( page->validationError() ) );
    }
    auto* language = dialog.findChild<QComboBox*>( QStringLiteral( "languageComboBox" ) );
    if ( language == nullptr ) {
        return fail( QStringLiteral( "Preferences has no languageComboBox" ) );
    }
    Configuration::get().setLanguage( language->currentData().toString() );
    QSignalSpy restartSpy{ &dialog, &OptionsDialog::restartRequested };
    auto* buttons = dialog.findChild<QDialogButtonBox*>( QStringLiteral( "buttonBox" ) );
    if ( buttons == nullptr ) {
        return fail( QStringLiteral( "Preferences has no buttonBox" ) );
    }

    if ( commandLine ) {
        auto* user = page->findChild<QRadioButton*>( QStringLiteral( "userStorageRadio" ) );
        auto* program = page->findChild<QRadioButton*>( QStringLiteral( "programStorageRadio" ) );
        auto* custom = page->findChild<QRadioButton*>( QStringLiteral( "customStorageRadio" ) );
        if ( user == nullptr || program == nullptr || custom == nullptr || user->isEnabled()
             || program->isEnabled() || custom->isEnabled() ) {
            return fail( QStringLiteral( "command-line managed storage controls are editable" ) );
        }
        buttons->button( QDialogButtonBox::Apply )->click();
        if ( restartSpy.count() != 0 || QFile::exists( store.programLocatorPath() )
             || QFile::exists( store.userLocatorPath() ) ) {
            return fail( QStringLiteral( "command-line managed Preferences wrote a locator" ) );
        }
        return 0;
    }

    if ( sameRootMode ) {
        auto* program = page->findChild<QRadioButton*>( QStringLiteral( "programStorageRadio" ) );
        if ( program == nullptr ) {
            return fail( QStringLiteral( "program storage selector is missing" ) );
        }
        program->click();
    }
    else {
        auto* custom = page->findChild<QRadioButton*>( QStringLiteral( "customStorageRadio" ) );
        auto* path = page->findChild<QLineEdit*>( QStringLiteral( "customStoragePath" ) );
        if ( custom == nullptr || path == nullptr ) {
            return fail( QStringLiteral( "custom storage controls are missing" ) );
        }
        custom->click();
        path->setText( scenario == QStringLiteral( "equivalent" )
                           ? QDir{ sourceRoot }.filePath( QStringLiteral( "." ) )
                           : QDir{ root }.filePath( QStringLiteral( "target" ) ) );
    }

    if ( scenario == QStringLiteral( "write-error" ) ) {
        if ( !QFile::remove( store.userLocatorPath() ) || !QDir{}.mkpath( store.userLocatorPath() ) ) {
            return fail( QStringLiteral( "failed to create isolated locator write failure" ) );
        }
        closeMessageBoxesSoon();
        buttons->button( QDialogButtonBox::Ok )->click();
        if ( dialog.result() == QDialog::Accepted || restartSpy.count() != 0 ) {
            return fail( QStringLiteral( "Preferences closed after locator write failure" ) );
        }
        return 0;
    }

    if ( scenario == QStringLiteral( "managed-target" ) ) {
        const StorageContext targetContext{ page->location() };
        if ( !targetContext.ensureDirectories( &error )
             || !StorageValidator::writeManifest( targetContext, &error ) ) {
            return fail( QStringLiteral( "failed to create managed target fixture: %1" )
                             .arg( error ) );
        }
        closeMessageBoxesSoon();
        buttons->button( QDialogButtonBox::Ok )->click();
        const auto resolution = store.resolve();
        if ( dialog.result() == QDialog::Accepted || restartSpy.count() != 0
             || ( resolution.state.has_value() && resolution.state->pending.has_value() ) ) {
            return fail( QStringLiteral( "non-empty managed target was scheduled for overwrite" ) );
        }
        return 0;
    }

    buttons->button( QDialogButtonBox::Apply )->click();
    const auto firstResolution = store.resolve();
    if ( scenario == QStringLiteral( "equivalent" ) ) {
        if ( firstResolution.state.has_value() && firstResolution.state->pending.has_value() ) {
            return fail( QStringLiteral( "equivalent storage path scheduled a migration" ) );
        }
        return restartSpy.count() == 0 ? 0
                                       : fail( QStringLiteral( "equivalent path requested restart" ) );
    }

    if ( !firstResolution.state.has_value() || !firstResolution.state->pending.has_value() ) {
        return fail( QStringLiteral( "storage change did not create pending migration: %1" )
                         .arg( firstResolution.error ) );
    }
    const StorageMigrationRequest pending = *firstResolution.state->pending;
    const auto& current = StorageContext::current();
    if ( pending.transactionId.isEmpty() || !samePath( pending.source.dataRoot, current.dataRoot() )
         || !samePath( pending.legacyConfigFile, current.configFilePath() )
         || !samePath( pending.legacySessionFile, current.sessionFilePath() )
         || !samePath( pending.sourceLogsDirectory, current.logsDirectory() )
         || !samePath( pending.legacyCrashDirectory, current.crashesDirectory() ) ) {
        return fail( QStringLiteral( "pending migration omitted current persistent data" ) );
    }
    if ( !samePath( StorageContext::current().dataRoot(), sourceRoot ) ) {
        return fail( QStringLiteral( "Preferences hot-switched StorageContext" ) );
    }
    const int expectedRestarts = scenario == QStringLiteral( "later" ) ? 0 : 1;
    if ( restartSpy.count() != expectedRestarts ) {
        return fail( QStringLiteral( "unexpected restartRequested count: %1" )
                         .arg( restartSpy.count() ) );
    }

    if ( scenario == QStringLiteral( "normal" ) ) {
        qApp->setProperty( "zzlogg.test.restartAnswer", QStringLiteral( "later" ) );
        buttons->button( QDialogButtonBox::Apply )->click();
        const auto repeated = store.resolve();
        if ( !repeated.state.has_value() || !repeated.state->pending.has_value()
             || repeated.state->pending->transactionId != pending.transactionId
             || restartSpy.count() != 1 ) {
            return fail( QStringLiteral( "repeated Apply replaced or duplicated pending state" ) );
        }
    }
    return 0;
}

} // namespace

class StorageSettingsTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void schedulesOnlySafeIsolatedChanges()
    {
        const QStringList scenarios{ QStringLiteral( "normal" ),
                                     QStringLiteral( "installed-runtime-paths" ),
                                     QStringLiteral( "later" ),
                                     QStringLiteral( "command-line" ),
                                     QStringLiteral( "equivalent" ),
                                     QStringLiteral( "same-root-mode" ),
                                     QStringLiteral( "managed-target" ),
                                     QStringLiteral( "write-error" ) };
        for ( const QString& scenario : scenarios ) {
            QTemporaryDir root;
            QVERIFY( root.isValid() );
            QProcess child;
            child.setProcessChannelMode( QProcess::MergedChannels );
            child.start( QCoreApplication::applicationFilePath(),
                         { QStringLiteral( "--zzlogg-storage-child" ), scenario, root.path(),
                           QStringLiteral( "-platform" ), QStringLiteral( "offscreen" ) } );
            QVERIFY2( child.waitForFinished( 30000 ), qPrintable( child.errorString() ) );
            QCOMPARE( child.exitStatus(), QProcess::NormalExit );
            QVERIFY2( child.exitCode() == 0,
                      qPrintable( QStringLiteral( "%1: %2" )
                                      .arg( scenario, QString::fromLocal8Bit( child.readAll() ) ) ) );
        }
    }
};

int main( int argc, char* argv[] )
{
    QStandardPaths::setTestModeEnabled( true );
    QApplication app{ argc, argv };
    if ( argc >= 4 && QString::fromLocal8Bit( argv[ 1 ] )
                         == QStringLiteral( "--zzlogg-storage-child" ) ) {
        return runChildScenario( QString::fromLocal8Bit( argv[ 2 ] ),
                                 QString::fromLocal8Bit( argv[ 3 ] ) );
    }
    StorageSettingsTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "storagesettingstest.moc"
