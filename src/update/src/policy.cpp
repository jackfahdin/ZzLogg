#include "zzlogg/update/policy.h"
#include <tuple>
namespace zzlogg::update {
Decision selectUpdate(const VerifiedManifest& verified, const InstalledRelease& current) {
    const auto& release=verified.manifest();
    if(release.channel!=current.channel) return {DecisionStatus::ChannelMismatch,{}};
    if(current.developmentBuild) return {DecisionStatus::DevelopmentBuild,{}};
    if(release.minUpdaterProtocol>current.updaterProtocol) return {DecisionStatus::ProtocolUnsupported,{}};
    if(current.dataSchema<release.minDataSchema || current.dataSchema>release.maxDataSchema)
        return {DecisionStatus::DataIncompatible,{}};
    const int version=compareVersion(release.version,current.version);
    if(release.releaseSequence==current.releaseSequence && version!=0)
        return {DecisionStatus::ReleaseConflict,{}};
    if(release.releaseSequence<=current.releaseSequence) return {};
    if(version<0 || (version==0 && current.channel!="preview"))
        return {DecisionStatus::ReleaseConflict,{}};
    const Artifact* matched=nullptr;
    for(const auto& artifact:release.artifacts) {
        if(artifact.os!=current.os || artifact.arch!=current.arch || artifact.distribution!=current.distribution)
            continue;
        if(!current.packageFormat.empty() && artifact.format!=current.packageFormat) continue;
        if(matched) return {DecisionStatus::NoCompatibleArtifact,{}};
        matched=&artifact;
    }
    if(matched) {
        const auto& artifact=*matched;
        const auto& min=artifact.minOsVersion;
        const auto& actual=current.osVersion;
        if(std::tie(actual.major,actual.minor,actual.patch)<std::tie(min.major,min.minor,min.patch))
            return {DecisionStatus::OsUnsupported,{}};
        return {DecisionStatus::Available,artifact};
    }
    return {DecisionStatus::NoCompatibleArtifact,{}};
}
}
