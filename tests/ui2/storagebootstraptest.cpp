#include "storagebootstrap.h"

#include "storagecontext.h"
#include "storagelocator.h"
#include "storagevalidator.h"
#include "zzlogg_brand.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

namespace {

struct Fixture {
    QString root;
    QString applicationDirectory;
    QString appConfigDirectory;
    QString legacyUserSettingsDirectory;
    QString userDataDirectory;
    QString oldCrashDirectory;
    StorageLocatorStore store;

    explicit Fixture( QString testRoot )
        : root( QDir::cleanPath( std::move( testRoot ) ) )
        , applicationDirectory( QDir{ root }.filePath( QStringLiteral( "app" ) ) )
        , appConfigDirectory( QDir{ root }.filePath( QStringLiteral( "settings" ) ) )
        , legacyUserSettingsDirectory(
              QDir{ root }.filePath( QStringLiteral( "legacy-user-settings" ) ) )
        , userDataDirectory( QDir{ root }.filePath( QStringLiteral( "user-data" ) ) )
        , oldCrashDirectory( QDir{ root }.filePath( QStringLiteral( "old-crashes" ) ) )
        , store( applicationDirectory, appConfigDirectory )
    {
        QDir{}.mkpath( applicationDirectory );
        QDir{}.mkpath( appConfigDirectory );
    }
};

bool writeFile( const QString& path, const QByteArray& bytes = QByteArrayLiteral( "[General]\n" ) )
{
    QDir{}.mkpath( QFileInfo{ path }.absolutePath() );
    QFile file{ path };
    return file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size();
}

bool prepareManagedRoot( const StorageLocation& location )
{
    const StorageContext context{ location };
    QString error;
    return context.ensureDirectories( &error )
           && StorageValidator::writeManifest( context, &error );
}

bool expect( bool condition, const QString& message )
{
    if ( condition ) {
        return true;
    }
    qCritical().noquote() << message;
    return false;
}

StorageBootstrapResult run( const Fixture& fixture, const QString& cli,
                            StorageSelectionProvider provider )
{
    return bootstrapStorage( fixture.applicationDirectory, fixture.appConfigDirectory,
                             fixture.userDataDirectory, fixture.legacyUserSettingsDirectory,
                             fixture.oldCrashDirectory, cli, std::move( provider ) );
}

int runScenario( const QString& name, const QString& root )
{
    Fixture fixture{ root };
    int providerCalls = 0;
    const auto neverSelect
        = [ & ]( const StorageBootstrapPrompt& ) -> std::optional<StorageLocation> {
        ++providerCalls;
        return StorageLocation{ StorageMode::UserDirectory, fixture.userDataDirectory, {}, false };
    };

    if ( name == QStringLiteral( "cli" ) ) {
        const QString dataRoot = QDir{ root }.filePath( QStringLiteral( "cli-data" ) );
        const auto result = run( fixture, dataRoot, neverSelect );
        const auto& location = StorageContext::current().location();
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( providerCalls == 0, QStringLiteral( "CLI invoked provider" ) )
                       && expect( location.commandLineOverride,
                                  QStringLiteral( "CLI override flag was not installed" ) )
                       && expect( location.dataRoot == QDir::cleanPath( dataRoot ),
                                  QStringLiteral( "wrong CLI root: %1" ).arg( location.dataRoot ) )
                       && expect( QFileInfo::exists( StorageContext::current().manifestFilePath() ),
                                  QStringLiteral( "CLI manifest missing: %1" )
                                      .arg( StorageContext::current().manifestFilePath() ) )
                       && expect( !QFileInfo::exists( fixture.store.programLocatorPath() )
                                      && !QFileInfo::exists( fixture.store.userLocatorPath() ),
                                  QStringLiteral( "CLI persisted a locator" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "relative-cli" ) ) {
        const QString relative = QStringLiteral( "relative/data" );
        const auto result = run( fixture, relative, neverSelect );
        return expect( result.status == StorageBootstrapStatus::Error,
                       QStringLiteral( "relative CLI path was accepted" ) )
                       && expect( result.error.contains( relative ),
                                  QStringLiteral( "relative CLI error omitted path: %1" )
                                      .arg( result.error ) )
                       && expect( providerCalls == 0,
                                  QStringLiteral( "relative CLI invoked provider" ) )
                       && expect( !StorageContext::isInstalled(),
                                  QStringLiteral( "relative CLI installed context" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "existing-locator" )
         || name == QStringLiteral( "locator-without-manifest" ) ) {
        const StorageLocation location{ StorageMode::UserDirectory, fixture.userDataDirectory,
                                        fixture.store.userLocatorPath(), false };
        if ( name == QStringLiteral( "existing-locator" ) && !prepareManagedRoot( location ) ) {
            return EXIT_FAILURE;
        }
        QString error;
        if ( !fixture.store.writeActive( location, &error ) ) {
            qCritical().noquote() << error;
            return EXIT_FAILURE;
        }
        const auto result = run( fixture, {}, neverSelect );
        const bool shouldSucceed = name == QStringLiteral( "existing-locator" );
        return expect(
                   ( result.status == StorageBootstrapStatus::Ready ) == shouldSucceed,
                   QStringLiteral( "unexpected existing-locator result: %1" ).arg( result.error ) )
                       && expect( providerCalls == 0, QStringLiteral( "locator invoked provider" ) )
                       && expect( shouldSucceed == StorageContext::isInstalled(),
                                  QStringLiteral( "locator installed unexpected context state" ) )
                       && expect(
                           shouldSucceed || result.error.contains( fixture.userDataDirectory ),
                           QStringLiteral( "manifest error omitted root: %1" ).arg( result.error ) )
                       && expect(
                           shouldSucceed || !QFileInfo::exists( fixture.userDataDirectory ),
                           QStringLiteral( "missing managed root was recreated: %1" )
                               .arg( fixture.userDataDirectory ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "corrupt-locator" ) ) {
        if ( !writeFile( fixture.store.programLocatorPath(), QByteArrayLiteral( "not-an-ini" ) ) ) {
            return EXIT_FAILURE;
        }
        const auto result = run( fixture, {}, neverSelect );
        return expect( result.status == StorageBootstrapStatus::Error,
                       QStringLiteral( "corrupt locator fell back" ) )
                       && expect( result.error.contains( fixture.store.programLocatorPath() ),
                                  QStringLiteral( "corrupt locator error omitted path: %1" )
                                      .arg( result.error ) )
                       && expect( providerCalls == 0,
                                  QStringLiteral( "corrupt locator invoked provider" ) )
                       && expect( !StorageContext::isInstalled(),
                                  QStringLiteral( "corrupt locator installed context" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "provider" ) ) {
        const auto result
            = run( fixture, {},
                   [ & ]( const StorageBootstrapPrompt& prompt ) -> std::optional<StorageLocation> {
                       ++providerCalls;
                       if ( prompt.applicationDirectory != fixture.applicationDirectory
                            || prompt.userDataDirectory != fixture.userDataDirectory ) {
                           return std::nullopt;
                       }
                       return StorageLocation{
                           StorageMode::UserDirectory, fixture.userDataDirectory, {}, false
                       };
                   } );
        const auto resolution = fixture.store.resolve();
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( providerCalls == 1,
                                  QStringLiteral( "provider call count is not one" ) )
                       && expect( resolution.state.has_value() && resolution.state->verified,
                                  QStringLiteral( "provider locator was not verified" ) )
                       && expect( QFileInfo::exists( fixture.store.userLocatorPath() ),
                                  QStringLiteral( "provider locator missing" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "cancelled" ) ) {
        const auto result = run(
            fixture, {}, [ & ]( const StorageBootstrapPrompt& ) -> std::optional<StorageLocation> {
                ++providerCalls;
                return std::nullopt;
            } );
        return expect( result.status == StorageBootstrapStatus::Cancelled,
                       QStringLiteral( "provider cancellation did not cancel" ) )
                       && expect( providerCalls == 1,
                                  QStringLiteral( "cancel provider call count" ) )
                       && expect( !StorageContext::isInstalled(),
                                  QStringLiteral( "cancelled provider installed context" ) )
                       && expect( !QFileInfo::exists( fixture.userDataDirectory )
                                      && !QFileInfo::exists( fixture.store.userLocatorPath() ),
                                  QStringLiteral( "cancelled provider wrote storage state" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "empty-provider" ) ) {
        const auto result = run( fixture, {}, {} );
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( StorageContext::current().location().mode
                                      == StorageMode::UserDirectory,
                                  QStringLiteral( "empty provider did not choose user directory" ) )
                       && expect( StorageContext::current().dataRoot()
                                      == QDir::cleanPath( fixture.userDataDirectory ),
                                  QStringLiteral( "empty provider chose wrong root" ) )
                       && expect( !QFileInfo::exists( fixture.store.userLocatorPath() ),
                                  QStringLiteral( "empty provider wrote locator" ) )
                       && expect( QFileInfo::exists( StorageContext::current().manifestFilePath() ),
                                  QStringLiteral( "empty provider manifest missing" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "legacy-priority" ) ) {
        const QString portableConfig
            = QDir{ fixture.applicationDirectory }.filePath( QStringLiteral( "ZzLogg.conf" ) );
        const QString userConfig
            = QDir{ fixture.appConfigDirectory }.filePath( QStringLiteral( "ZzLogg.ini" ) );
        if ( !writeFile( portableConfig, QByteArrayLiteral( "[legacy]\nkind=portable\n" ) )
             || !writeFile( userConfig, QByteArrayLiteral( "[legacy]\nkind=user\n" ) ) ) {
            return EXIT_FAILURE;
        }
        const auto result = run( fixture, {}, neverSelect );
        const QString target
            = QDir{ fixture.applicationDirectory }.filePath( QStringLiteral( "data" ) );
        const QString copiedConfig = StorageContext{
            { StorageMode::ProgramDirectory, target, {}, false }
        }.configFilePath();
        QFile copied{ copiedConfig };
        const bool copiedPortable
            = copied.open( QIODevice::ReadOnly ) && copied.readAll().contains( "kind=portable" );
        const auto resolution = fixture.store.resolve();
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( providerCalls == 0, QStringLiteral( "legacy invoked provider" ) )
                       && expect( StorageContext::current().dataRoot() == QDir::cleanPath( target ),
                                  QStringLiteral( "portable legacy target was wrong" ) )
                       && expect( copiedPortable,
                                  QStringLiteral( "portable legacy config not copied" ) )
                       && expect( QFileInfo::exists( portableConfig ),
                                  QStringLiteral( "legacy source was deleted" ) )
                       && expect( resolution.state.has_value() && !resolution.state->verified,
                                  QStringLiteral( "migration target unexpectedly verified" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "explicit-user-legacy-settings-path" ) ) {
        const QString legacyConfig = QDir{ fixture.legacyUserSettingsDirectory }.filePath(
            QStringLiteral( "ZzLogg.ini" ) );
        const QString legacySession = QDir{ fixture.legacyUserSettingsDirectory }.filePath(
            QStringLiteral( "ZzLogg_session.ini" ) );
        if ( !writeFile( legacyConfig, QByteArrayLiteral( "[legacy]\nsource=qsettings\n" ) )
             || !writeFile( legacySession ) ) {
            return EXIT_FAILURE;
        }

        const auto result = run( fixture, {}, neverSelect );
        const StorageContext targetContext{ { StorageMode::UserDirectory, fixture.userDataDirectory,
                                              fixture.store.userLocatorPath(), false } };
        QFile copied{ targetContext.configFilePath() };
        const bool copiedUserLegacy
            = copied.open( QIODevice::ReadOnly ) && copied.readAll().contains( "source=qsettings" );
        const auto resolution = fixture.store.resolve();
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( providerCalls == 0,
                                  QStringLiteral( "explicit user legacy invoked provider" ) )
                       && expect( StorageContext::current().dataRoot()
                                      == QDir::cleanPath( fixture.userDataDirectory ),
                                  QStringLiteral( "explicit user legacy target was wrong" ) )
                       && expect( copiedUserLegacy,
                                  QStringLiteral( "explicit user legacy was not copied" ) )
                       && expect( QFileInfo::exists( legacyConfig ),
                                  QStringLiteral( "QSettings user legacy source was deleted" ) )
                       && expect( QFileInfo::exists( fixture.store.userLocatorPath() )
                                      && QFileInfo::exists( targetContext.manifestFilePath() ),
                                  QStringLiteral( "QSettings user migration state missing" ) )
                       && expect( resolution.state.has_value() && !resolution.state->verified,
                                  QStringLiteral( "QSettings migration unexpectedly verified" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "empty-provider-ignores-legacy" ) ) {
        const QString portableConfig
            = QDir{ fixture.applicationDirectory }.filePath( QStringLiteral( "ZzLogg.conf" ) );
        const QString userConfig = QDir{ fixture.legacyUserSettingsDirectory }.filePath(
            QStringLiteral( "ZzLogg.ini" ) );
        const QByteArray portableBytes{ "[legacy]\nsource=portable-must-stay\n" };
        const QByteArray userBytes{ "[legacy]\nsource=user-must-stay\n" };
        if ( !writeFile( portableConfig, portableBytes ) || !writeFile( userConfig, userBytes ) ) {
            return EXIT_FAILURE;
        }

        const auto result = run( fixture, {}, {} );
        QFile portableAfter{ portableConfig };
        QFile userAfter{ userConfig };
        const bool legacyUnchanged
            = portableAfter.open( QIODevice::ReadOnly ) && portableAfter.readAll() == portableBytes
              && userAfter.open( QIODevice::ReadOnly ) && userAfter.readAll() == userBytes;
        const StorageContext defaultContext{
            { StorageMode::UserDirectory, fixture.userDataDirectory, {}, false }
        };
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( StorageContext::current().dataRoot()
                                      == QDir::cleanPath( fixture.userDataDirectory ),
                                  QStringLiteral( "empty provider migrated legacy root" ) )
                       && expect( legacyUnchanged,
                                  QStringLiteral( "empty provider changed legacy bytes" ) )
                       && expect( !QFileInfo::exists( fixture.store.programLocatorPath() )
                                      && !QFileInfo::exists( fixture.store.userLocatorPath() ),
                                  QStringLiteral( "empty provider persisted a legacy locator" ) )
                       && expect( QFileInfo::exists( defaultContext.manifestFilePath() )
                                      && !QFileInfo::exists( defaultContext.configFilePath() ),
                                  QStringLiteral( "empty provider copied legacy data" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "provider-write-failure" ) ) {
        const QString target = QDir{ root }.filePath( QStringLiteral( "provider-data" ) );
        const bool preexistingManifest
            = QCoreApplication::arguments().contains( QStringLiteral( "--preexisting-manifest" ) );
        if ( preexistingManifest
             && !prepareManagedRoot( { StorageMode::UserDirectory, target, {}, false } ) ) {
            return EXIT_FAILURE;
        }
        const auto result = run(
            fixture, {}, [ & ]( const StorageBootstrapPrompt& ) -> std::optional<StorageLocation> {
                ++providerCalls;
                QDir{}.mkpath( fixture.store.programLocatorPath() );
                return StorageLocation{ StorageMode::UserDirectory, target, {}, false };
            } );
        const QString manifest = StorageContext{
            { StorageMode::UserDirectory, target, {}, false }
        }.manifestFilePath();
        return expect( result.status == StorageBootstrapStatus::Error,
                       QStringLiteral( "provider locator failure was accepted" ) )
                       && expect( result.error.contains( fixture.store.programLocatorPath() ),
                                  QStringLiteral( "provider write error omitted path: %1" )
                                      .arg( result.error ) )
                       && expect( providerCalls == 1,
                                  QStringLiteral( "provider write call count" ) )
                       && expect(
                           QFileInfo::exists( manifest ) == preexistingManifest,
                           preexistingManifest
                               ? QStringLiteral( "preexisting manifest was deleted: %1" )
                                     .arg( manifest )
                               : QStringLiteral( "new manifest survived locator failure: %1" )
                                     .arg( manifest ) )
                       && expect( !StorageContext::isInstalled(),
                                  QStringLiteral( "provider failure installed context" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "pending-recovery" ) ) {
        const StorageLocation source{ StorageMode::UserDirectory,
                                      QDir{ root }.filePath( QStringLiteral( "source" ) ),
                                      fixture.store.userLocatorPath(), false };
        const StorageLocation target{ StorageMode::ProgramDirectory,
                                      QDir{ fixture.applicationDirectory }.filePath(
                                          QStringLiteral( "data" ) ),
                                      fixture.store.programLocatorPath(), false };
        const StorageMigrationRequest request{
            QUuid::createUuid().toString( QUuid::WithoutBraces ),
            source,
            target,
            QDir{ source.dataRoot }.filePath( QStringLiteral( "ZzLogg.ini" ) ),
            QDir{ source.dataRoot }.filePath( QStringLiteral( "ZzLogg_session.ini" ) ),
            QDir{ source.dataRoot }.filePath( QStringLiteral( "crashes" ) )
        };
        QString error;
        if ( !prepareManagedRoot( source ) || !fixture.store.writeActive( source, &error )
             || !fixture.store.writePending( request, &error ) ) {
            qCritical().noquote() << error;
            return EXIT_FAILURE;
        }
        const StorageContext targetContext{ target };
        if ( !targetContext.ensureDirectories( &error )
             || !writeFile( targetContext.configFilePath() )
             || !writeFile( targetContext.sessionFilePath() )
             || !StorageValidator::writeManifest( targetContext, &error ) ) {
            qCritical().noquote() << error;
            return EXIT_FAILURE;
        }
        const auto result = run( fixture, {}, neverSelect );
        const auto resolution = fixture.store.resolve();
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( providerCalls == 0,
                                  QStringLiteral( "pending recovery invoked provider" ) )
                       && expect( StorageContext::current().dataRoot()
                                      == QDir::cleanPath( target.dataRoot ),
                                  QStringLiteral( "pending recovery installed wrong root" ) )
                       && expect( resolution.state.has_value()
                                      && !resolution.state->pending.has_value()
                                      && !resolution.state->verified,
                                  QStringLiteral( "pending recovery state was not committed" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    if ( name == QStringLiteral( "pending-rollback-retry" ) ) {
        const StorageLocation source{ StorageMode::UserDirectory, fixture.appConfigDirectory,
                                      fixture.store.userLocatorPath(), false };
        const StorageLocation target{ StorageMode::ProgramDirectory,
                                      QDir{ fixture.applicationDirectory }.filePath(
                                          QStringLiteral( "data" ) ),
                                      fixture.store.programLocatorPath(), false };
        const QString legacyConfig
            = QDir{ source.dataRoot }.filePath( QStringLiteral( "ZzLogg.ini" ) );
        const QString legacySession
            = QDir{ source.dataRoot }.filePath( QStringLiteral( "ZzLogg_session.ini" ) );
        if ( !writeFile( legacyConfig, QByteArrayLiteral( "[legacy]\nretry=true\n" ) )
             || !writeFile( legacySession ) ) {
            return EXIT_FAILURE;
        }
        const StorageMigrationRequest request{ QUuid::createUuid().toString( QUuid::WithoutBraces ),
                                               source,
                                               target,
                                               legacyConfig,
                                               legacySession,
                                               fixture.oldCrashDirectory };
        QString error;
        if ( !fixture.store.writeActive( source, &error )
             || !fixture.store.writePending( request, &error ) ) {
            qCritical().noquote() << error;
            return EXIT_FAILURE;
        }
        const auto result = run( fixture, {}, neverSelect );
        QFile copied{ StorageContext{ target }.configFilePath() };
        const bool retriedCopy
            = copied.open( QIODevice::ReadOnly ) && copied.readAll().contains( "retry=true" );
        return expect( result.status == StorageBootstrapStatus::Ready, result.error )
                       && expect( retriedCopy,
                                  QStringLiteral( "rolled-back request was not re-executed" ) )
                       && expect( providerCalls == 0,
                                  QStringLiteral( "pending rollback invoked provider" ) )
                       && expect( QFileInfo::exists( legacyConfig ),
                                  QStringLiteral( "retry deleted legacy source" ) )
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    qCritical().noquote() << "unknown scenario:" << name;
    return EXIT_FAILURE;
}

} // namespace

bool executeScenarioProcess( const QString& name, const QStringList& extraArguments = {} )
{
    QTemporaryDir temporaryDirectory;
    if ( !temporaryDirectory.isValid() ) {
        qCritical().noquote() << name << ": failed to create temporary directory";
        return false;
    }
    QProcess process;
    process.setProgram( QCoreApplication::applicationFilePath() );
    QStringList arguments{ QStringLiteral( "--bootstrap-scenario" ), name,
                           temporaryDirectory.path() };
    arguments.append( extraArguments );
    process.setArguments( arguments );
    process.start();
    if ( !process.waitForFinished( 30000 ) ) {
        qCritical().noquote() << name << ":" << process.errorString();
        return false;
    }
    if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != EXIT_SUCCESS ) {
        qCritical().noquote() << name << ":" << process.readAllStandardError();
        return false;
    }
    qInfo().noquote() << "PASS" << name;
    return true;
}

int main( int argc, char* argv[] )
{
    QCoreApplication app{ argc, argv };
    const QStringList arguments = app.arguments();
    const int scenarioIndex = arguments.indexOf( QStringLiteral( "--bootstrap-scenario" ) );
    if ( scenarioIndex >= 0 && arguments.size() > scenarioIndex + 2 ) {
        return runScenario( arguments.at( scenarioIndex + 1 ), arguments.at( scenarioIndex + 2 ) );
    }
    const QStringList scenarios{
        QStringLiteral( "cli" ),
        QStringLiteral( "relative-cli" ),
        QStringLiteral( "existing-locator" ),
        QStringLiteral( "locator-without-manifest" ),
        QStringLiteral( "corrupt-locator" ),
        QStringLiteral( "provider" ),
        QStringLiteral( "cancelled" ),
        QStringLiteral( "empty-provider" ),
        QStringLiteral( "legacy-priority" ),
        QStringLiteral( "explicit-user-legacy-settings-path" ),
        QStringLiteral( "empty-provider-ignores-legacy" ),
        QStringLiteral( "provider-write-failure" ),
        QStringLiteral( "pending-recovery" ),
        QStringLiteral( "pending-rollback-retry" ),
    };
    for ( const QString& scenario : scenarios ) {
        if ( !executeScenarioProcess( scenario ) ) {
            return EXIT_FAILURE;
        }
    }
    if ( !executeScenarioProcess( QStringLiteral( "provider-write-failure" ),
                                  { QStringLiteral( "--preexisting-manifest" ) } ) ) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
