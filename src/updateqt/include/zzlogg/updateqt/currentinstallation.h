#pragma once

#include "zzlogg/updateqt/installedrelease.h"

namespace zzlogg::updateqt {

// 纯组合，供测试注入三项输入。任意一项不满足即返回 std::nullopt。
std::optional<update::InstalledRelease> composeInstalledRelease(
    const std::optional<update::ReleaseIdentity>& release,
    const InstallationIdentity& installation,
    const update::OsVersion& osVersion);

// 生产入口：编译进来的发布身份 + 当前安装探测 + 当前系统版本。
std::optional<update::InstalledRelease> currentInstalledRelease();

} // namespace zzlogg::updateqt
