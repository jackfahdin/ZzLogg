#pragma once
#include <cstdint>

namespace zzlogg::updater::timing {
// Shared by the GUI, relay and transaction engine. Receivers include the
// sender's whole phase plus IPC margin, not just one of its sub-steps.
inline constexpr std::uint32_t ControlMs=5000;
inline constexpr std::uint32_t InstallerLaunchMs=300000;
inline constexpr std::uint32_t EnginePrepareMs=180000;
inline constexpr std::uint32_t PackageVerificationAllowanceMs=120000;
inline constexpr std::uint32_t ReadyForExitMs=
    PackageVerificationAllowanceMs+InstallerLaunchMs+EnginePrepareMs+2*ControlMs;
inline constexpr std::uint32_t CommitExitMs=300000;
inline constexpr std::uint32_t ApplicationExitMs=300000;
inline constexpr std::uint32_t EngineCommitWaitMs=CommitExitMs+2*ControlMs;
inline constexpr std::uint32_t EngineProceedWaitMs=ApplicationExitMs+2*ControlMs;
inline constexpr std::uint32_t InstallMs=600000;
inline constexpr std::uint32_t RestartMs=60000;
}
