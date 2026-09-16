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
    // Compiled policy only. Intentionally empty until real signing certificates
    // and their revocation/rotation procedures have passed release acceptance.
    const detail::PublisherPolicy publishers;
    if(publishers.empty()) return {{},PackageVerificationError::PublisherPolicyMissing};
    auto lease=std::make_unique<VerifiedPackage::Impl>();
    auto error=lease->file.open(path);
    if(error!=PackageVerificationError::None) return {{},error};
    error=lease->file.verifyContent(selected.artifact->size,selected.artifact->sha256);
    if(error!=PackageVerificationError::None) return {{},error};
    error=detail::verifyAuthenticode(lease->file.handle(),lease->file.path(),publishers);
    if(error!=PackageVerificationError::None) return {{},error};
    if(!lease->file.identityUnchanged()) return {{},PackageVerificationError::FileUnavailable};
    return {VerifiedPackage(std::move(lease)),PackageVerificationError::None};
}
}
