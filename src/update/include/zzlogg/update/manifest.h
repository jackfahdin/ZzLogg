#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "version.h"

namespace zzlogg::update {
enum class TrustEnvironment { Production, Test };
enum class KeyPurpose { Production, Test };
struct TrustedKey {
    std::string id;
    std::vector<std::uint8_t> publicKey;
    KeyPurpose purpose = KeyPurpose::Production;
};
struct AcceptedMetadata {
    std::uint64_t sequence = 0;
    std::array<std::uint8_t,64> payloadDigest{};
};
struct VerificationContext {
    std::vector<TrustedKey> keys;
    TrustEnvironment environment = TrustEnvironment::Production;
    std::vector<std::string> allowedHosts;
    std::int64_t now = 0;
    std::int64_t buildTime = 0;
    std::string channel = "stable";
    std::optional<AcceptedMetadata> lastAccepted;
};
enum class VerificationError {
    None, EnvelopeInvalid, TrustInvalid, SignatureInvalid, PayloadInvalid,
    ClockInvalid, Expired, ChannelMismatch, Replay, MetadataConflict
};
enum class Distribution { Portable, Installer };
struct OsVersion { std::uint32_t major=0, minor=0, patch=0; };
struct Artifact {
    std::string os, arch;
    Distribution distribution=Distribution::Portable;
    std::string format;
    OsVersion minOsVersion;
    std::string url;
    std::uint64_t size=0;
    std::string sha256;
};
struct Manifest {
    std::uint64_t metadataSequence=0, releaseSequence=0;
    std::int64_t issuedAt=0, expiresAt=0;
    std::string channel;
    Version version{};
    std::uint32_t minUpdaterProtocol=0, minDataSchema=0, maxDataSchema=0;
    std::array<std::string,3> notes;
    std::vector<Artifact> artifacts;
};
struct VerificationResult;
class VerifiedManifest {
public:
    const Manifest& manifest() const { return manifest_; }
    const AcceptedMetadata& acceptedMetadata() const { return accepted_; }
    TrustEnvironment trustEnvironment() const { return environment_; }
    const TrustedKey& signingKey() const { return signingKey_; }
private:
    VerifiedManifest(Manifest manifest, AcceptedMetadata accepted, TrustEnvironment environment,
                     TrustedKey signingKey)
        : manifest_(std::move(manifest)), accepted_(accepted), environment_(environment),
          signingKey_(std::move(signingKey)) {}
    Manifest manifest_;
    AcceptedMetadata accepted_;
    TrustEnvironment environment_;
    TrustedKey signingKey_;
    friend VerificationResult verifyManifest(std::string_view, const VerificationContext&);
};
struct VerificationResult {
    std::optional<VerifiedManifest> value;
    VerificationError error=VerificationError::PayloadInvalid;
};
VerificationResult verifyManifest(std::string_view envelope, const VerificationContext&);
}
