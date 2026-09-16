#pragma once
#include "executionselection.h"
#include <memory>

namespace zzlogg::update {
enum class PackageVerificationError { None, Unsupported, SelectionRejected,
    PublisherPolicyMissing, InvalidPath, FileUnavailable, SizeMismatch,
    HashMismatch, SignatureUntrusted, PublisherMismatch };
struct PackageVerificationResult;
class VerifiedPackage {
public:
    ~VerifiedPackage();
    VerifiedPackage(VerifiedPackage&&) noexcept;
    VerifiedPackage& operator=(VerifiedPackage&&) noexcept;
    const std::wstring& path() const;
private:
    struct Impl;
    explicit VerifiedPackage(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend PackageVerificationResult verifyPackageForExecution(const UpdateSelection&,
        const VerificationContext&, const InstalledRelease&, const std::wstring&);
};
struct PackageVerificationResult {
    std::optional<VerifiedPackage> package;
    PackageVerificationError error=PackageVerificationError::Unsupported;
};
PackageVerificationResult verifyPackageForExecution(const UpdateSelection&,
    const VerificationContext&, const InstalledRelease&, const std::wstring& path);
}
