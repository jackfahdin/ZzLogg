#pragma once

#include "zzlogg/update/manifest.h"

namespace zzlogg::updateqt {

// 当前操作系统版本。Windows 上 patch 是构建号（清单的 minOsVersion 按构建号比较）；
// 其他平台返回全零，makeInstalledRelease 据此拒绝组合已安装发布身份。
update::OsVersion currentOsVersion();

} // namespace zzlogg::updateqt
