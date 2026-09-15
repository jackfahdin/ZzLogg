#pragma once
#include "manifest.h"
namespace zzlogg::update {
struct InstalledRelease {
    Version version{};
    std::uint64_t releaseSequence=0;
    bool developmentBuild=false;
    std::string channel="stable", os="windows", arch="x64";
    Distribution distribution=Distribution::Portable;
    OsVersion osVersion{};
    std::uint32_t dataSchema=0, updaterProtocol=1;
};
enum class DecisionStatus {
    Available, ChannelMismatch, DevelopmentBuild, ProtocolUnsupported, DataIncompatible,
    NoUpdate, ReleaseConflict, NoCompatibleArtifact, OsUnsupported
};
struct Decision {
    DecisionStatus status=DecisionStatus::NoUpdate;
    std::optional<Artifact> artifact;
};
Decision selectUpdate(const VerifiedManifest&, const InstalledRelease&);
}
