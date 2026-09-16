#include "zzlogg/update/executionselection.h"

namespace zzlogg::update {
namespace {
bool validVersion(Version version)
{
    return version.year<=99 && version.month>=1 && version.month<=12 && version.patch<=99;
}

bool eligibleInstalledRelease(const InstalledRelease& current)
{
    return !current.developmentBuild && current.releaseSequence!=0
        && validVersion(current.version)
        && (current.channel=="stable" || current.channel=="preview")
        && current.os=="windows" && current.arch=="x64"
        && current.distribution==Distribution::Installer
        && current.osVersion.major!=0;
}

bool sameAccepted(const AcceptedMetadata& left,const AcceptedMetadata& right)
{
    return left.sequence==right.sequence && left.payloadDigest==right.payloadDigest;
}

bool sameArtifact(const Artifact& left,const Artifact& right)
{
    return left.os==right.os && left.arch==right.arch
        && left.distribution==right.distribution && left.format==right.format
        && left.minOsVersion.major==right.minOsVersion.major
        && left.minOsVersion.minor==right.minOsVersion.minor
        && left.minOsVersion.patch==right.minOsVersion.patch
        && left.url==right.url && left.size==right.size && left.sha256==right.sha256;
}
}

std::optional<UpdateSelection> makeUpdateSelection(
    const VerifiedManifest& verified,const InstalledRelease& current)
{
    if(!eligibleInstalledRelease(current)) return std::nullopt;
    const auto decision=selectUpdate(verified,current);
    if(decision.status!=DecisionStatus::Available || !decision.artifact
        || decision.artifact->os!="windows" || decision.artifact->arch!="x64"
        || decision.artifact->distribution!=Distribution::Installer
        || decision.artifact->format!="nsis-exe") return std::nullopt;
    return UpdateSelection{verified.signedEnvelope(),verified.acceptedMetadata(),
        verified.manifest().releaseSequence,*decision.artifact};
}

SelectionResult revalidateUpdateSelection(const UpdateSelection& selection,
    const VerificationContext& freshContext,const InstalledRelease& current)
{
    if(!freshContext.lastAccepted) return {{},SelectionError::StateMissing};
    const auto verified=verifyManifest(selection.signedEnvelope,freshContext);
    if(!verified.value) return {{},SelectionError::VerificationFailed};
    const auto expected=makeUpdateSelection(*verified.value,current);
    if(!expected) return {{},SelectionError::Unavailable};
    if(!sameAccepted(selection.accepted,expected->accepted)
        || selection.releaseSequence!=expected->releaseSequence
        || !sameArtifact(selection.artifact,expected->artifact))
        return {{},SelectionError::TargetChanged};
    return {expected->artifact,SelectionError::None};
}
}
