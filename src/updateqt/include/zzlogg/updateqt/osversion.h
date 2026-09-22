#pragma once

#include "zzlogg/update/manifest.h"

namespace zzlogg::updateqt {

// 从平台报告的三个整数组合版本。任一分量非正都返回全零：
// 真实 Windows 没有 0 号构建，零值只代表探测失败。
update::OsVersion makeOsVersion(int major, int minor, int micro);

// 当前操作系统版本。Windows 上 patch 是构建号（清单的 minOsVersion 按构建号比较）；
// 其他平台返回全零，makeInstalledRelease 据此拒绝组合已安装发布身份。
update::OsVersion currentOsVersion();

} // namespace zzlogg::updateqt
