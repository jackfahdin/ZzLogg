#pragma once

#include "policy.h"

namespace zzlogg::update {
struct UpdateSelection {
    std::string signedEnvelope;
    AcceptedMetadata accepted;
    std::uint64_t releaseSequence=0;
    Artifact artifact;
};

std::optional<UpdateSelection> makeUpdateSelection(
    const VerifiedManifest&, const InstalledRelease&);

enum class SelectionError {
    None, StateMissing, VerificationFailed, TargetChanged, Unavailable
};

struct SelectionResult {
    std::optional<Artifact> artifact;
    SelectionError error=SelectionError::Unavailable;
};

SelectionResult revalidateUpdateSelection(const UpdateSelection&,
    const VerificationContext& freshContext, const InstalledRelease& current);
}
