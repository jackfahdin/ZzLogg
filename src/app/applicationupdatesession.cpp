#include "applicationupdatesession.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include "bootstrap_win_p.h"
#include "coordinator_p.h"
#include "storagecontext.h"
#endif

namespace {
bool lowercaseHexDigest( const QString& value )
{
    if ( value.size() != 64 ) {
        return false;
    }
    for ( const QChar character : value ) {
        if ( !(( character >= QLatin1Char( '0' ) && character <= QLatin1Char( '9' ) )
               || ( character >= QLatin1Char( 'a' ) && character <= QLatin1Char( 'f' ) )) ) {
            return false;
        }
    }
    return true;
}

#ifdef Q_OS_WIN
namespace updater = zzlogg::updater::detail;

class CoordinatorSession final : public ApplicationUpdateHandoff::CoordinationSession {
  public:
    CoordinatorSession( std::wstring image, std::wstring base, updater::HandoffRequest request,
                        std::optional<zzlogg::updater::DirectoryIdentity> reserved )
        : image_( std::move( image ) )
        , base_( std::move( base ) )
        , request_( std::move( request ) )
        , reserved_( reserved )
    {
    }

    ApplicationUpdateHandoff::Failure start() override
    {
        const auto* reserved = reserved_ ? &*reserved_ : nullptr;
        if ( !coordinator_.start( image_, base_, reserved, nullptr, request_ ) ) {
            return ApplicationUpdateHandoff::Failure::LaunchFailed;
        }
        if ( !coordinator_.authenticate( updater::after( 10000 ) ) ) {
            return ApplicationUpdateHandoff::Failure::PeerRejected;
        }
        const auto waiting = coordinator_.awaitAppExit( updater::after( 300000 ) );
        if ( waiting == updater::CoordinationResult::WaitingForAppExit ) {
            return ApplicationUpdateHandoff::Failure::None;
        }
        return failureFromChildExit();
    }
    bool canCommitExit() const override
    {
        return coordinator_.canCommitExit();
    }
    bool commitExit() override
    {
        return coordinator_.commitExit( updater::after( 5000 ) );
    }
    void cancel() override
    {
        coordinator_.cancel( updater::after( 5000 ) );
    }

  private:
    ApplicationUpdateHandoff::Failure failureFromChildExit() const
    {
        DWORD code = 0;
        const auto handle = coordinator_.process().handle();
        if ( handle && WaitForSingleObject( handle, 5000 ) == WAIT_OBJECT_0
             && GetExitCodeProcess( handle, &code ) ) {
            if ( code == static_cast<DWORD>( updater::ElevationDeclined ) ) {
                return ApplicationUpdateHandoff::Failure::ApprovalDeclined;
            }
            if ( code == static_cast<DWORD>( updater::InstallerLaunchFailed ) ) {
                return ApplicationUpdateHandoff::Failure::LaunchFailed;
            }
        }
        return ApplicationUpdateHandoff::Failure::PeerLost;
    }

    updater::Coordinator coordinator_;
    std::wstring image_;
    std::wstring base_;
    updater::HandoffRequest request_;
    std::optional<zzlogg::updater::DirectoryIdentity> reserved_;
};
#endif
} // namespace

ApplicationUpdateHandoff::SessionFactory makeUpdateSessionFactory(
    const ApplicationUpdateSessionInputs& inputs )
{
    if ( !inputs.releaseIdentityAvailable || inputs.registeredInstallRoot.isEmpty()
         || inputs.verifiedPackagePath.isEmpty() || inputs.packageSize == 0
         || !lowercaseHexDigest( inputs.packageSha256 ) ) {
        return {};
    }
#ifdef Q_OS_WIN
    return []( const ApplicationUpdateHandoff::Request& request )
               -> std::unique_ptr<ApplicationUpdateHandoff::CoordinationSession> {
        const auto image = QDir( QCoreApplication::applicationDirPath() )
                               .filePath( QStringLiteral( "ZzLoggUpdate.exe" ) );
        const auto base = QDir( QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) )
                              .filePath( QStringLiteral( "updates/production/runtime-v1" ) );
        if ( !QFileInfo::exists( image ) || !QDir().mkpath( base ) ) {
            return nullptr;
        }
        const auto digest = QByteArray::fromHex( request.packageSha256.toLatin1() );
        if ( digest.size() != 32 ) {
            return nullptr;
        }
        updater::HandoffRequest native;
        native.dataDirectory
            = QDir::toNativeSeparators( StorageContext::current().runtimePaths().appConfigDirectory )
                  .toStdWString();
        native.installerPath = QDir::toNativeSeparators( request.packagePath ).toStdWString();
        native.installRoot = QDir::toNativeSeparators( request.installRoot ).toStdWString();
        native.packageSize = request.packageSize;
        std::copy( digest.begin(), digest.end(), native.packageSha256.begin() );
        return std::make_unique<CoordinatorSession>(
            QDir::toNativeSeparators( image ).toStdWString(),
            QDir::toNativeSeparators( base ).toStdWString(), std::move( native ), request.directory );
    };
#else
    return {};
#endif
}
