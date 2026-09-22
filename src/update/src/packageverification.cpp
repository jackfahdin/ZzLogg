#include "stablepackage_p.h"
#include "authenticode_p.h"
#include "executionplatform_p.h"
namespace zzlogg::update {
struct VerifiedPackage::Impl { detail::StablePackage file; };
VerifiedPackage::VerifiedPackage(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
VerifiedPackage::~VerifiedPackage()=default;
VerifiedPackage::VerifiedPackage(VerifiedPackage&&) noexcept=default;
VerifiedPackage& VerifiedPackage::operator=(VerifiedPackage&&) noexcept=default;
const std::wstring& VerifiedPackage::path() const { static const std::wstring empty; return impl_?impl_->file.path():empty; }
PackageVerificationResult verifyPackageForExecution(const UpdateSelection& selection,const VerificationContext& context,
    const InstalledRelease& current,const std::wstring& path) {
    if(!detail::executionPlatformSupported) return {};
    const auto selected=revalidateUpdateSelection(selection,context,current);
    if(!selected.artifact || context.environment!=TrustEnvironment::Production)
        return {{},PackageVerificationError::SelectionRejected};
    // This project owns no code signing certificate: the size and SHA-256 carried by
    // the Ed25519-signed manifest are the execution trust root. A non-empty fingerprint
    // list layers Authenticode publisher pinning on top, with no other change needed.
    const detail::PublisherPolicy publishers;
    auto lease=std::make_unique<VerifiedPackage::Impl>();
    auto error=lease->file.open(path);
    if(error!=PackageVerificationError::None) return {{},error};
    error=lease->file.verifyContent(selected.artifact->size,selected.artifact->sha256);
    if(error!=PackageVerificationError::None) return {{},error};
    if(!publishers.empty()) {
        error=detail::verifyAuthenticode(lease->file.handle(),lease->file.path(),publishers);
        if(error!=PackageVerificationError::None) return {{},error};
    }
    if(!lease->file.identityUnchanged()) return {{},PackageVerificationError::FileUnavailable};
    return {VerifiedPackage(std::move(lease)),PackageVerificationError::None};
}
}
