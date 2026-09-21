# Windows 稳定版一键安装更新 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让稳定版渠道的 Windows x64 注册安装用户在"检查更新"对话框点击"退出并安装更新"后，自动完成下载、校验、提权、事务替换与重启。

**架构：** 仓库里的受限事务链路已经完整存在（`TxEngine` 写前日志与回滚、`Coordinator::startInstaller` 提权启动与引擎通道、Inno Setup 的 `/ZzLoggUpgrade=<locator>` 受限入口），本计划只补齐生产路径上的四处断点：启用官方发布身份、把已安装发布身份接进更新服务、把包校验的信任根从 Authenticode 换成清单锚定的 SHA-256、把 `ZzLoggUpdate.exe` 从"握手后拒绝"改成真正的协调者中继并接上生产会话工厂。

**技术栈：** C++17、Qt 6、CMake、Win32（WinTrust／ShellExecuteEx／命名管道）、Inno Setup 7.1、GitHub Actions。

**设计依据：** [docs/superpowers/specs/2026-09-21-windows-one-click-update-design.md](../specs/2026-09-21-windows-one-click-update-design.md)

---

## 开发环境约束（先读这一节）

开发机是 Linux，本计划中**任务 4 到任务 7 的 C++ 代码在本机无法编译**：`src/updater`
整个目录、`src/update` 的执行子库、`tests/update/packageverificationtest.cpp` 都在
`if(WIN32)` 之后（见 `src/CMakeLists.txt:13`、`tests/update/CMakeLists.txt:27`）。

这些任务的验证方式是推分支跑 Windows CI：

```bash
git push -u origin <分支名>
gh workflow run "CI Build" --ref <分支名>
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

本机可以完整验证的是任务 1、2、3、8、9。每个任务的运行步骤会写明属于哪一类。
本机 Linux 构建与测试的标准命令（后续任务直接引用，不再重复）：

```bash
cmake --build /你的构建目录 --target ci_build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 --output-on-failure -j4
```

**一条无法自动化的边界：** 真实 UAC 提权之后的链路（安装器解包、引擎握手、事务执行、
重启确认）在 CI 上跑不了——生产二进制不接受注入启动器，而 CI runner 不能弹 UAC。
任务 6 用一个专用测试目标覆盖中继时序，真实提权链路留给末尾的人工验收。

## 文件结构

**任务 1 — 发布身份构建开关**

- 修改：`cmake/ZzReleaseIdentity.cmake` — 环境变量默认值 + 发布序号派生函数
- 创建：`tests/update/releasesequencetest.cmake` — 派生算法的本机单元测试
- 修改：`tests/update/CMakeLists.txt`、`tests/update/releaseconfigurationtest.cmake`
- 修改：`.github/workflows/ci-build.yml`、`.github/workflows/publish.yml`

**任务 2 — 系统版本探测**

- 创建：`src/updateqt/include/zzlogg/updateqt/osversion.h`、`src/updateqt/src/osversion.cpp`
- 修改：`src/updateqt/CMakeLists.txt`
- 测试：`tests/updateqt/osversiontest.cpp`、`tests/updateqt/CMakeLists.txt`

**任务 3 — 已安装发布身份接线**

- 创建：`src/updateqt/include/zzlogg/updateqt/currentinstallation.h`、`src/updateqt/src/currentinstallation.cpp`
- 修改：`src/updateqt/CMakeLists.txt`、`src/app/kloggapp.h`
- 测试：`tests/updateqt/currentinstallationtest.cpp`、`tests/updateqt/CMakeLists.txt`

**任务 4 — 包校验信任根（Windows）**

- 修改：`src/update/src/packageverification.cpp`
- 测试：`tests/update/packageverificationtest.cpp`

**任务 5 — bootstrap v4 与协调者退出码（Windows）**

- 修改：`src/updater/bootstrap_win_p.h`、`src/updater/bootstrap_win.cpp`
- 修改：`src/updater/coordinator_p.h`、`src/updater/coordinator.cpp`
- 测试：`tests/updater/coordinatortest.cpp`

**任务 6 — 协调者生产中继（Windows）**

- 重写：`src/updater/main.cpp`
- 修改：`src/updater/CMakeLists.txt`、`tests/updater/CMakeLists.txt`
- 测试：`tests/updater/relaytest.cpp`

**任务 7 — 生产会话工厂（Windows）**

- 创建：`src/app/applicationupdatesession.h`、`src/app/applicationupdatesession.cpp`
- 修改：`src/app/applicationupdatehandoff.h`、`src/app/applicationupdatehandoff.cpp`
- 修改：`src/app/kloggapp.h`、`src/app/CMakeLists.txt`
- 测试：`tests/ui_acceptance/updatehandofftest.cpp`

**任务 8 — 界面文案**

- 修改：`src/ui/src/updatecheckdialog.cpp`
- 测试：`tests/ui_acceptance/updatecheckuitest.cpp`

**任务 9 — 文档与变更记录**

- 修改：`docs/development/GITHUB_UPDATES.md`、`docs/development/UPDATE_PROTOCOL.md`、`docs/development/UPDATE_ACCEPTANCE_VM.md`、`CHANGELOG.md`

---

### 任务 1：发布身份构建开关与序号派生

发布身份从未被任何工作流打开，`compiledReleaseIdentity()` 恒为空。本任务让它由环境变量
驱动，并让发布序号从显示版本自动派生。派生必须与 `scripts/ci/publish_update_feed.py:266`
的 `int(version.replace('.', ''))` 逐字符等价——两侧算出不同的数会让 `selectUpdate` 判为
`ReleaseConflict`，症状是"检查到新版本却拒绝安装"，而且只在真实发布时才暴露。

**文件：**
- 修改：`cmake/ZzReleaseIdentity.cmake`
- 创建：`tests/update/releasesequencetest.cmake`
- 修改：`tests/update/CMakeLists.txt`、`tests/update/releaseconfigurationtest.cmake`
- 修改：`.github/workflows/ci-build.yml`、`.github/workflows/publish.yml`

- [ ] **步骤 1：编写失败的测试——派生算法本身**

创建 `tests/update/releasesequencetest.cmake`。它直接 include 模块并调用派生函数，
因此在 Linux 上也能真正断言数值，不依赖 Windows 目标：

```cmake
cmake_minimum_required(VERSION 3.23)
if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "Release sequence test requires SOURCE_ROOT")
endif()
include("${SOURCE_ROOT}/cmake/ZzReleaseIdentity.cmake")

# 与 scripts/ci/publish_update_feed.py 的 int(version.replace('.', '')) 等价，
# 包括前导零剥离。两侧不一致会让 selectUpdate 判为 ReleaseConflict。
function(expect_sequence version expected)
  _zzlogg_derive_release_sequence("${version}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${version}: expected ${expected}, got ${actual}")
  endif()
endfunction()

expect_sequence("26.09.03" "260903")
expect_sequence("26.12.00" "261200")
expect_sequence("09.01.00" "90100")
expect_sequence("00.01.00" "100")
```

在 `tests/update/CMakeLists.txt` 的 `zzlogg_release_configuration_test("")` 那一行之前插入：

```cmake
add_test(NAME zzlogg_update.release_sequence COMMAND "${CMAKE_COMMAND}"
  "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/releasesequencetest.cmake")
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake -S . -B /你的构建目录 >/dev/null
ctest --test-dir /你的构建目录 -R 'zzlogg_update.release_sequence' --output-on-failure
```

预期：FAIL，报 `Unknown CMake command "_zzlogg_derive_release_sequence"`。

- [ ] **步骤 3：实现派生函数与环境变量默认值**

`cmake/ZzReleaseIdentity.cmake` 顶部的选项声明替换为：

```cmake
set(official_release_default OFF)
if(DEFINED ENV{ZZLOGG_OFFICIAL_RELEASE})
  set(official_release_default "$ENV{ZZLOGG_OFFICIAL_RELEASE}")
endif()
option(ZZLOGG_OFFICIAL_RELEASE "Build with an official ZzLogg release identity"
  ${official_release_default})
set(ZZLOGG_RELEASE_SEQUENCE "" CACHE STRING
  "Official release sequence; empty derives it from the display version")
set(ZZLOGG_RELEASE_CHANNEL "$ENV{ZZLOGG_RELEASE_CHANNEL}" CACHE STRING
  "Official release channel")
set(ZZLOGG_RELEASE_DATA_SCHEMA "0" CACHE STRING "Official release data schema")
```

在 `_zzlogg_validate_release_uint` 之后加入派生函数：

```cmake
# 与 scripts/ci/publish_update_feed.py 的 int(version.replace('.', '')) 等价。
function(_zzlogg_derive_release_sequence display_version output)
  string(REPLACE "." "" derived "${display_version}")
  string(REGEX REPLACE "^0+" "" derived "${derived}")
  if(derived STREQUAL "")
    set(derived "0")
  endif()
  set(${output} "${derived}" PARENT_SCOPE)
endfunction()
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake -S . -B /你的构建目录 >/dev/null
ctest --test-dir /你的构建目录 -R 'zzlogg_update.release_sequence' --output-on-failure
```

预期：PASS，四个版本全部匹配。

- [ ] **步骤 5：在配置流程中接入派生，并调整被推翻的旧用例**

`zzlogg_configure_release_identity` 的 `if(ZZLOGG_OFFICIAL_RELEASE)` 分支里，把原本位于
`_zzlogg_validate_release_uint` 之后的 `ZZLOGG_DISPLAY_VERSION` 形状校验**整块上移到分支
开头**（派生依赖它已经合法），随后插入派生，再做原有的 uint 校验：

```cmake
  if(ZZLOGG_OFFICIAL_RELEASE)
    if(NOT "${ZZLOGG_DISPLAY_VERSION}" MATCHES
        "^[0-9][0-9]\\.(0[1-9]|1[0-2])\\.[0-9][0-9]$")
      message(FATAL_ERROR
        "Official release identity: ZZLOGG_DISPLAY_VERSION must be YY.MM.PP")
    endif()
    if(ZZLOGG_RELEASE_SEQUENCE STREQUAL "")
      _zzlogg_derive_release_sequence("${ZZLOGG_DISPLAY_VERSION}" ZZLOGG_RELEASE_SEQUENCE)
    endif()
    _zzlogg_validate_release_uint(
      "ZZLOGG_RELEASE_SEQUENCE" "${ZZLOGG_RELEASE_SEQUENCE}"
      "18446744073709551615")
```

原位置那份重复的显示版本校验块删掉，其余校验（sequence 非零、data schema、channel、
Windows、x64、tweak 为零）原样保留。

`tests/update/releaseconfigurationtest.cmake` 第 219 行的
`run_invalid_case(missing_sequence EMPTY_SEQUENCE)` 现在被推翻了——空序号不再是错误。
删掉这一行，并在 `run_release_case(stable ...)` 之后（`if(TEST_NATIVE_CASES_ONLY)` 之前）
加入完整配置路径上的派生用例：

```cmake
# 空序号不再是错误：它从显示版本派生。Linux 上 native_official_supported 为 OFF，
# 该用例退化为"官方身份在非 Windows 目标上失败关闭"，数值断言由
# zzlogg_update.release_sequence 独立覆盖。
run_release_case(derived_sequence ${native_official_supported}
  OFFICIAL ON SEQUENCE "" CHANNEL stable SCHEMA 0 VERSION 26.09.03 TWEAK 0
  EXPECT_AVAILABLE ON EXPECT_SEQUENCE 260903 EXPECT_SCHEMA 0 EXPECT_CHANNEL stable)
```

- [ ] **步骤 6：运行完整配置测试**

```bash
ctest --test-dir /你的构建目录 -R 'release_configuration|release_sequence' --output-on-failure
```

预期：全部 PASS。`missing_channel`、`missing_schema` 等其余 invalid 用例不受影响——
它们显式传 `-D...=` 空值，会覆盖新的缓存默认值。

- [ ] **步骤 7：让 CI 只在稳定版发布时打开开关**

`.github/workflows/ci-build.yml` 的 `workflow_call` 增加输入：

```yaml
  workflow_call:
    inputs:
      source_sha:
        description: Exact commit selected by the release workflow
        required: true
        type: string
      release_mode:
        description: stable 时构建官方发布身份；其他值保持关闭
        required: false
        default: ''
        type: string
```

同一文件 `Windows:` job（第 336 行）在 `strategy:` 之前插入 job 级环境变量。**不要**放到
工作流顶层 `env:`：`zzlogg_configure_release_identity` 在非 Windows 目标上会
`FATAL_ERROR`，那会直接打断 Linux 与 macOS job。

```yaml
  Windows:
    if: "inputs.source_sha != '' || !contains(github.event.head_commit.message, '[skip ci]')"
    env:
      # 只有稳定版发布构建取得官方发布身份；PR 与预览构建保持关闭。
      ZZLOGG_OFFICIAL_RELEASE: ${{ inputs.release_mode == 'stable' && 'ON' || 'OFF' }}
      ZZLOGG_RELEASE_CHANNEL: stable
    strategy:
```

`.github/workflows/publish.yml` 的 `build:` job 传入模式：

```yaml
  build:
    needs: prepare
    if: needs.prepare.outputs.changed == 'true'
    uses: ./.github/workflows/ci-build.yml
    with:
      source_sha: ${{ needs.prepare.outputs.sha }}
      release_mode: ${{ needs.prepare.outputs.mode }}
    secrets: inherit
```

- [ ] **步骤 8：校验工作流语法并 commit**

```bash
python3 -c "import yaml; [yaml.safe_load(open(f)) for f in ['.github/workflows/ci-build.yml','.github/workflows/publish.yml']]; print('ok')"
git add cmake/ZzReleaseIdentity.cmake tests/update/releasesequencetest.cmake \
  tests/update/CMakeLists.txt tests/update/releaseconfigurationtest.cmake \
  .github/workflows/ci-build.yml .github/workflows/publish.yml
git commit -m "feat: 稳定版发布构建启用官方发布身份并派生发布序号"
```

---

### 任务 2：系统版本探测

`makeInstalledRelease` 要求 `osVersion.major != 0`（`src/updateqt/src/installedrelease.cpp:32`），
而清单要求 `minOsVersion` 为 `10.0.19041`（`scripts/ci/publish_update_feed.py:269`），
所以必须拿到真实构建号，不能只给主次版本。

**文件：**
- 创建：`src/updateqt/include/zzlogg/updateqt/osversion.h`、`src/updateqt/src/osversion.cpp`
- 修改：`src/updateqt/CMakeLists.txt`
- 测试：`tests/updateqt/osversiontest.cpp`、`tests/updateqt/CMakeLists.txt`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/updateqt/osversiontest.cpp`：

```cpp
#include <QtTest>
#include "zzlogg/updateqt/osversion.h"

using namespace zzlogg::updateqt;

class OsVersionTest : public QObject {
    Q_OBJECT
private slots:
    void reportsBuildNumberOnWindowsAndZeroElsewhere() {
        const auto version = currentOsVersion();
#ifdef Q_OS_WIN
        // 清单的 minOsVersion 是 10.0.19041，补丁位必须是真实构建号而不是 0。
        QVERIFY(version.major >= 10);
        QVERIFY(version.patch >= 10240);
#else
        QCOMPARE(version.major, 0u);
        QCOMPARE(version.minor, 0u);
        QCOMPARE(version.patch, 0u);
#endif
    }
};
QTEST_GUILESS_MAIN(OsVersionTest)
#include "osversiontest.moc"
```

在 `tests/updateqt/CMakeLists.txt` 的 `zzlogg_update_qt_test(state updatestatetest.cpp)`
之后加一行：

```cmake
zzlogg_update_qt_test(os_version osversiontest.cpp)
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build /你的构建目录 --target zzlogg_update_os_version_test
```

预期：FAIL，编译期报找不到 `zzlogg/updateqt/osversion.h`。

- [ ] **步骤 3：实现探测**

创建 `src/updateqt/include/zzlogg/updateqt/osversion.h`：

```cpp
#pragma once

#include "zzlogg/update/manifest.h"

namespace zzlogg::updateqt {

// 当前操作系统版本。Windows 上 patch 是构建号（清单的 minOsVersion 按构建号比较）；
// 其他平台返回全零，makeInstalledRelease 据此拒绝组合已安装发布身份。
update::OsVersion currentOsVersion();

} // namespace zzlogg::updateqt
```

创建 `src/updateqt/src/osversion.cpp`：

```cpp
#include "zzlogg/updateqt/osversion.h"

#include <QOperatingSystemVersion>

namespace zzlogg::updateqt {

update::OsVersion currentOsVersion()
{
#ifdef Q_OS_WIN
    const auto current = QOperatingSystemVersion::current();
    const auto major = current.majorVersion();
    const auto minor = current.minorVersion();
    const auto micro = current.microVersion();
    if (major <= 0 || minor < 0 || micro < 0) return {};
    return {static_cast<std::uint32_t>(major), static_cast<std::uint32_t>(minor),
            static_cast<std::uint32_t>(micro)};
#else
    return {};
#endif
}

} // namespace zzlogg::updateqt
```

在 `src/updateqt/CMakeLists.txt` 的源文件列表加入 `src/osversion.cpp`，照抄相邻
`src/installedrelease.cpp` 的写法。

- [ ] **步骤 4：运行测试验证通过**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 -R 'zzlogg_update.os_version' --output-on-failure
```

预期：PASS（Linux 上走全零分支）。

- [ ] **步骤 5：Commit**

```bash
git add src/updateqt/include/zzlogg/updateqt/osversion.h src/updateqt/src/osversion.cpp \
  src/updateqt/CMakeLists.txt tests/updateqt/osversiontest.cpp tests/updateqt/CMakeLists.txt
git commit -m "feat: 新增更新服务使用的系统版本探测"
```

---

### 任务 3：已安装发布身份接线

`KloggApp::ensureUpdateService` 给两个更新服务传的 `installed` 都是 `std::nullopt`
（`src/app/kloggapp.h:507,511`），`UpdateDownloadService::trustedNow` 因此永远返回 false
（`src/updateqt/src/updatedownloadservice.cpp:64`），"下载更新"一直不可用。

**文件：**
- 创建：`src/updateqt/include/zzlogg/updateqt/currentinstallation.h`、`src/updateqt/src/currentinstallation.cpp`
- 修改：`src/updateqt/CMakeLists.txt`、`src/app/kloggapp.h`
- 测试：`tests/updateqt/currentinstallationtest.cpp`、`tests/updateqt/CMakeLists.txt`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/updateqt/currentinstallationtest.cpp`：

```cpp
#include <QtTest>
#include "zzlogg/updateqt/currentinstallation.h"

using namespace zzlogg::updateqt;

namespace {
update::ReleaseIdentity official()
{
    return {*update::parseVersion("26.09.03"), 260903, "stable", "windows", "x64", 0, 1};
}
}

class CurrentInstallationTest : public QObject {
    Q_OBJECT
private slots:
    // 三项输入缺一不可：非官方构建、非注册安装、系统版本探测失败，
    // 任意一项都必须失败关闭，而不是构造出一个半真的已安装身份。
    void composesOnlyWithEveryInput() {
        const InstallationIdentity registered{InstallationKind::Registered,
                                              "C:\\Program Files\\ZzLogg"};
        const InstallationIdentity unregistered{InstallationKind::Unregistered, {}};
        QVERIFY(!composeInstalledRelease(official(), unregistered, {10, 0, 22631}).has_value());
        QVERIFY(!composeInstalledRelease(std::nullopt, registered, {10, 0, 22631}).has_value());
        QVERIFY(!composeInstalledRelease(official(), registered, {}).has_value());
        const auto composed = composeInstalledRelease(official(), registered, {10, 0, 22631});
        QVERIFY(composed.has_value());
        QCOMPARE(composed->releaseSequence, 260903ull);
        QCOMPARE(composed->distribution, update::Distribution::Installer);
    }
    void nonOfficialBuildYieldsNothing() {
        // 本机与 PR 构建都不带官方发布身份，生产入口必须返回空。
        QVERIFY(!currentInstalledRelease().has_value());
    }
};
QTEST_GUILESS_MAIN(CurrentInstallationTest)
#include "currentinstallationtest.moc"
```

在 `tests/updateqt/CMakeLists.txt` 加一行：

```cmake
zzlogg_update_qt_test(current_installation currentinstallationtest.cpp)
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build /你的构建目录 --target zzlogg_update_current_installation_test
```

预期：FAIL，编译期报找不到 `zzlogg/updateqt/currentinstallation.h`。

- [ ] **步骤 3：实现组合单元**

创建 `src/updateqt/include/zzlogg/updateqt/currentinstallation.h`：

```cpp
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
```

创建 `src/updateqt/src/currentinstallation.cpp`：

```cpp
#include "zzlogg/updateqt/currentinstallation.h"

#include "zzlogg/updateqt/osversion.h"

namespace zzlogg::updateqt {

std::optional<update::InstalledRelease> composeInstalledRelease(
    const std::optional<update::ReleaseIdentity>& release,
    const InstallationIdentity& installation,
    const update::OsVersion& osVersion)
{
    return makeInstalledRelease(release, installation, osVersion);
}

std::optional<update::InstalledRelease> currentInstalledRelease()
{
    return composeInstalledRelease(update::compiledReleaseIdentity(),
                                   probeCurrentInstallation(), currentOsVersion());
}

} // namespace zzlogg::updateqt
```

在 `src/updateqt/CMakeLists.txt` 的源文件列表加入 `src/currentinstallation.cpp`。

- [ ] **步骤 4：运行测试验证通过**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 -R 'zzlogg_update.current_installation' --output-on-failure
```

预期：PASS。

- [ ] **步骤 5：在 KloggApp 中使用**

`src/app/kloggapp.h` 顶部加入包含：

```cpp
#include "zzlogg/updateqt/currentinstallation.h"
```

`ensureUpdateService()` 中两处 `std::nullopt` 换成同一个已安装身份——两个服务必须拿到同一
个值，否则检查与下载会对不同的安装身份做判断：

```cpp
    void ensureUpdateService() {
        using namespace zzlogg::updateqt;
        if (updateService_ || !StorageContext::isInstalled()) return;
        const auto installed=currentInstalledRelease();
        const auto root=StorageContext::current().runtimePaths().appConfigDirectory;
        const auto path=root.isEmpty() ? QString{} : QDir(root).filePath("updates/production/check-state-v1.json");
        updateService_=std::make_unique<UpdateService>(productionFeedConfiguration(),
            std::make_shared<UpdateStateStore>(path),installed,
```

以及下面构造 `UpdateDownloadService` 时的第二个实参由 `std::nullopt` 改为 `installed`。

- [ ] **步骤 6：跑完整套件确认无回归**

```bash
cmake --build /你的构建目录 --target ci_build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 --output-on-failure -j4
```

预期：全部 PASS。Linux 上 `currentInstalledRelease()` 返回 `std::nullopt`，行为与改动前
完全一致。

- [ ] **步骤 7：Commit**

```bash
git add src/updateqt/include/zzlogg/updateqt/currentinstallation.h \
  src/updateqt/src/currentinstallation.cpp src/updateqt/CMakeLists.txt \
  src/app/kloggapp.h tests/updateqt/currentinstallationtest.cpp tests/updateqt/CMakeLists.txt
git commit -m "feat: 更新服务接入已安装发布身份"
```

---

### 任务 4：包校验改用清单锚定的信任根（Windows）

`verifyPackageForExecution` 第一件事就是拿故意留空的发布者指纹列表判
`PublisherPolicyMissing`（`src/update/src/packageverification.cpp:19-20`），任何生产输入
都过不去。改为以签名清单给出的大小与 SHA-256 作为执行信任根；指纹列表非空时仍叠加
Authenticode。

**文件：**
- 修改：`src/update/src/packageverification.cpp`
- 测试：`tests/update/packageverificationtest.cpp`

- [ ] **步骤 1：改写测试，把"恒定拒绝"翻转为"哈希锚定"**

把 `tests/update/packageverificationtest.cpp:141` 的
`productionCompositionRemainsClosed` 整个方法替换为：

```cpp
    void productionCompositionAnchorsOnManifest() {
        static_assert(!std::is_default_constructible_v<VerifiedPackage>);
        static_assert(!std::is_copy_constructible_v<VerifiedPackage>);
        static_assert(std::is_nothrow_move_constructible_v<VerifiedPackage>);
        Files files;
        SelectionFixture test;
        QVERIFY(!test.selection.signedEnvelope.empty());
        auto result=verifyPackageForExecution(test.selection,test.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        SelectionFixture production(true);
        QVERIFY(!production.selection.signedEnvelope.empty());
        // 清单锚定的字节匹配即授权执行；没有证书时这是唯一的执行信任根。
        result=verifyPackageForExecution(production.selection,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::None); QVERIFY(result.package);
        // 路径校验不再被缺失的发布者策略短路。
        result=verifyPackageForExecution(production.selection,production.context,installed(),L"not a path");
        QCOMPARE(result.error,PackageVerificationError::InvalidPath); QVERIFY(!result.package);
        const auto rewrite=[&](const QByteArray& bytes) {
            QFile output(QString::fromStdWString(files.file));
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(bytes),qint64(bytes.size()));
        };
        // 同长度不同内容必须判哈希不符；长度不同必须判大小不符。
        rewrite("abd");
        result=verifyPackageForExecution(production.selection,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::HashMismatch); QVERIFY(!result.package);
        rewrite("abcd");
        result=verifyPackageForExecution(production.selection,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SizeMismatch); QVERIFY(!result.package);
        rewrite("abc");
        // 签名选择本身的每一条防线保持不变。
        auto altered=production.selection; altered.signedEnvelope[0]='x';
        result=verifyPackageForExecution(altered,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        auto stale=production.context; stale.now=1800003600;
        result=verifyPackageForExecution(production.selection,stale,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        stale=production.context; ++stale.lastAccepted->sequence;
        result=verifyPackageForExecution(production.selection,stale,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        auto changed=installed(); changed.arch="arm64";
        result=verifyPackageForExecution(production.selection,production.context,changed,files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
    }
```

`authenticodeAlgorithm` 与 `realUnsignedFileIsRejected` **不要动**：`checkAuthenticode`
对空策略仍返回 `PublisherPolicyMissing`，这层算法语义不变，变的只是上层是否调用它。

- [ ] **步骤 2：推分支跑 Windows CI 验证失败**

```bash
git add tests/update/packageverificationtest.cpp
git commit -m "test: 包校验以清单锚定哈希为执行信任根"
git push -u origin <分支名>
gh workflow run "CI Build" --ref <分支名>
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

预期：`zzlogg_update.package_verification` FAIL，实际得到 `PublisherPolicyMissing`
而期望 `None`。

- [ ] **步骤 3：实现**

`src/update/src/packageverification.cpp` 的 `verifyPackageForExecution` 整体替换为：

```cpp
PackageVerificationResult verifyPackageForExecution(const UpdateSelection& selection,const VerificationContext& context,
    const InstalledRelease& current,const std::wstring& path) {
    if(!detail::executionPlatformSupported) return {};
    const auto selected=revalidateUpdateSelection(selection,context,current);
    if(!selected.artifact || context.environment!=TrustEnvironment::Production)
        return {{},PackageVerificationError::SelectionRejected};
    // 项目没有代码签名证书：Ed25519 签名清单给出的大小与 SHA-256 是执行信任根。
    // 指纹列表非空时在其之上叠加 Authenticode 发布者钉扎，无需其他改动。
    const detail::PublisherPolicy publishers;
    auto lease=std::make_unique<VerifiedPackage::Impl>();
    auto error=lease->file.open(path);
    if(error!=PackageVerificationError::None) return {{},error};
    error=lease->file.verifyContent(selected.artifact->size,selected.artifact->sha256);
    if(error!=PackageVerificationError::None) return {{},error};
    if(!publishers.empty()) {
        error=detail::verifyAuthenticode(lease->file.handle(),lease->file.path(),publishers);
        if(error!=PackageVerificationError::None) return {{},error};
    }
    if(!lease->file.identityUnchanged()) return {{},PackageVerificationError::FileUnavailable};
    return {VerifiedPackage(std::move(lease)),PackageVerificationError::None};
}
```

- [ ] **步骤 4：推送并验证 Windows CI 通过**

```bash
git add src/update/src/packageverification.cpp
git commit -m "feat: 包校验以签名清单锚定的哈希授权执行"
git push
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

预期：Windows job 全绿。

---

### 任务 5：bootstrap v4 与协调者退出码（Windows）

协调者要启动安装器就必须知道安装包路径与安装根，而 `BootstrapData` 现在只带
`dataDirectory`（`src/updater/bootstrap_win_p.h:12-25`）。按设计裁决，协调者不把"父进程说
它校验过了"当依据，因此期望大小与 SHA-256 也随 bootstrap 下传。同时为协调者进程定义自己
的退出码——`txcontract_win_p.h` 里的 42–48 属于事务引擎与安装器，不能复用。

**文件：**
- 修改：`src/updater/bootstrap_win_p.h`、`src/updater/bootstrap_win.cpp`
- 修改：`src/updater/coordinator_p.h`、`src/updater/coordinator.cpp`
- 测试：`tests/updater/coordinatortest.cpp`

- [ ] **步骤 1：编写失败的测试**

`tests/updater/coordinatortest.cpp` 中标题为 `coordinator bootstrap v3` 的块
（第 446 行起）改名为 v4，`retired.version=2` 改为 `retired.version=3`，其余
`dataDirectory` 断言原样保留，并追加新字段用例：

```cpp
        std::cout<<"coordinator bootstrap v4"<<std::endl;
        // ...原有 dataDirectory 用例保持不变，retired.version 改为 3...
        auto retired=base;retired.version=3;
        check(spawnMappedFixture(source,retired)==51,"retired version 3 mapping rejected");
        auto installer=base;
        {const auto file=(root/L"setup.exe").wstring();std::copy(file.begin(),file.end(),installer.installerPath);
         const auto target=root.wstring();std::copy(target.begin(),target.end(),installer.installRoot);
         installer.packageSize=3;installer.packageSha256[0]=1;}
        check(spawnMappedFixture(source,installer)==52,"absolute installer path and install root accepted");
        auto relativeInstaller=installer;
        {const wchar_t text[]=L"relative\\setup.exe";
         std::fill(std::begin(relativeInstaller.installerPath),std::end(relativeInstaller.installerPath),L'\0');
         std::copy(text,text+_countof(text),relativeInstaller.installerPath);}
        check(spawnMappedFixture(source,relativeInstaller)==51,"relative installer path rejected");
        auto halfSet=installer;
        std::fill(std::begin(halfSet.installRoot),std::end(halfSet.installRoot),L'\0');
        check(spawnMappedFixture(source,halfSet)==51,"installer path without an install root rejected");
        auto zeroSize=installer;zeroSize.packageSize=0;
        check(spawnMappedFixture(source,zeroSize)==51,"installer fields without a package size rejected");
        auto oversize=installer;oversize.packageSize=512ull*1024*1024+1;
        check(spawnMappedFixture(source,oversize)==51,"package size beyond the lease limit rejected");
        auto zeroDigest=installer;
        std::fill(std::begin(zeroDigest.packageSha256),std::end(zeroDigest.packageSha256),uint8_t(0));
        check(spawnMappedFixture(source,zeroDigest)==51,"all-zero package digest rejected");
```

- [ ] **步骤 2：推分支跑 Windows CI 验证失败**

```bash
git add tests/updater/coordinatortest.cpp
git commit -m "test: bootstrap v4 携带安装包路径、安装根与期望字节"
git push && gh workflow run "CI Build" --ref <分支名>
```

预期：编译失败，`BootstrapData` 没有 `installerPath` 成员。

- [ ] **步骤 3：实现 v4 数据结构与协调者退出码**

`src/updater/bootstrap_win_p.h`：把 `DataDirectoryCapacity` 统一为 `PathCapacity`，扩展
结构体，并补齐协调者进程自己的退出码表。

```cpp
inline constexpr int ExecutionDisabled=40;
inline constexpr int BootstrapRejected=41;
// 协调者进程（ZzLoggUpdate.exe）自己的退出码。事务引擎与 Inno 受限入口占用
// 42-48（txcontract_win_p.h），这里从 60 起，两套码位不重叠。0 = 已安装并重启。
inline constexpr int PackageRejected=60;       // bootstrap 声明的字节与实际不符
inline constexpr int ElevationDeclined=61;     // 用户在 UAC 上拒绝
inline constexpr int InstallerLaunchFailed=62; // 提权启动本身失败
inline constexpr int RelayFailed=63;           // 握手、协议或事务失败
inline constexpr int RestartPending=64;        // 已安装但应用未被重启

// 固定容量、进程私有的匿名映射；argv 里只有它的数值读句柄。
inline constexpr std::size_t PathCapacity=240;
struct BootstrapData {
    uint32_t magic=0x42555a5a,version=4;
    uint64_t parentHandle=0;
    ProcessStamp parent{};
    TransactionId transaction{};
    SessionToken token{};
    // 预留的安装目录身份；仅在 DirectoryReserved 时有效。
    DirectoryIdentity directory{};
    uint32_t flags=0;
    // 事务后重启用的受限数据目录。空表示没有；否则是容量内 NUL 结尾、
    // 不含控制字符的绝对路径。
    wchar_t dataDirectory[PathCapacity]{};
    // 已校验安装包与登记安装目录。整组齐备或整组缺席，不接受半套。
    wchar_t installerPath[PathCapacity]{};
    wchar_t installRoot[PathCapacity]{};
    // 签名清单给出的期望字节。协调者据此独立复核，不信任父进程的结论。
    uint64_t packageSize=0;
    uint8_t packageSha256[32]{};
};
// GUI -> 协调者的子进程启动请求。安装链路字段为空即退化为纯协议握手。
struct ChildLaunchRequest {
    TransactionId transaction{};
    SessionToken token{};
    const DirectoryIdentity* reservedIdentity=nullptr;
    std::wstring dataDirectory;
    std::wstring installerPath;
    std::wstring installRoot;
    uint64_t packageSize=0;
    std::array<uint8_t,32> packageSha256{};
};
bool launchCopy(const RuntimeCopy&,const ChildLaunchRequest&,ProcessIdentity&);
// 绝对本地或 UNC 路径，不含控制字符。三个路径字段共用这条规则。
bool absolutePathPlausible(const wchar_t* text,std::size_t length);
```

删除旧的 `DataDirectoryCapacity`、`dataDirectoryPlausible` 与旧 `launchCopy` 声明，
并在文件顶部加上 `#include <array>`。

- [ ] **步骤 4：实现 v4 解析与写入**

`src/updater/bootstrap_win.cpp`：`dataDirectoryPlausibleImpl` 改名
`absolutePathPlausibleImpl`（函数体不变），导出名同步为 `absolutePathPlausible`，并加入
长度助手：

```cpp
// 固定容量数组里的 NUL 结尾长度；未终止返回 PathCapacity。
std::size_t boundedLength(const wchar_t* text) {
    std::size_t length=0;
    while(length<PathCapacity && text[length])++length;
    return length;
}
```

`ChildBootstrap::open` 的校验段替换为：

```cpp
    const auto directoryLength=boundedLength(data.dataDirectory);
    const auto installerLength=boundedLength(data.installerPath);
    const auto rootLength=boundedLength(data.installRoot);
    const auto digestSet=std::any_of(std::begin(data.packageSha256),std::end(data.packageSha256),
        [](auto byte){return byte!=0;});
    const bool installerPresent=installerLength || rootLength || data.packageSize || digestSet;
    // 安装链路字段要么整组缺席，要么整组齐备且各自合法；半套一律拒绝。
    const bool installerConsistent=!installerPresent
        || (installerLength && installerLength<PathCapacity
            && absolutePathPlausible(data.installerPath,installerLength)
            && rootLength && rootLength<PathCapacity
            && absolutePathPlausible(data.installRoot,rootLength)
            && data.packageSize && data.packageSize<=512ull*1024*1024 && digestSet);
    if(data.magic!=0x42555a5a || data.version!=4 || (data.flags&~DirectoryReserved) || !data.parentHandle
        || directoryLength==PathCapacity || !absolutePathPlausible(data.dataDirectory,directoryLength)
        || !installerConsistent
        || endpointName(data.transaction).empty()
        || !encodeMessage({MessageKind::Hello,data.transaction,data.token}))return false;
```

`launchCopy` 改用请求结构：

```cpp
bool launchCopy(const RuntimeCopy& copy,const ChildLaunchRequest& request,ProcessIdentity& child){
    const auto pathFits=[](const std::wstring& value){
        return value.size()<PathCapacity && absolutePathPlausible(value.data(),value.size()); };
    if(!copy.unchanged() || !pathFits(request.dataDirectory))return false;
    const bool installerPresent=!request.installerPath.empty();
    if(installerPresent && (!pathFits(request.installerPath) || request.installRoot.empty()
        || !pathFits(request.installRoot) || !request.packageSize
        || request.packageSize>512ull*1024*1024
        || std::all_of(request.packageSha256.begin(),request.packageSha256.end(),
               [](auto byte){return byte==0;})))
        return false;
    // ...父进程句柄复制与映射创建保持不变...
    BootstrapData data;data.parentHandle=reinterpret_cast<uint64_t>(inheritedParent.get());
    data.parent=parent.stamp();data.transaction=request.transaction;data.token=request.token;
    if(request.reservedIdentity){data.flags=DirectoryReserved;data.directory=*request.reservedIdentity;}
    std::copy(request.dataDirectory.begin(),request.dataDirectory.end(),data.dataDirectory);
    if(installerPresent) {
        std::copy(request.installerPath.begin(),request.installerPath.end(),data.installerPath);
        std::copy(request.installRoot.begin(),request.installRoot.end(),data.installRoot);
        data.packageSize=request.packageSize;
        std::copy(request.packageSha256.begin(),request.packageSha256.end(),data.packageSha256);
    }
    // ...其余保持不变...
```

`absolutePathPlausible` 对空串返回 true，所以空 `dataDirectory` 仍然合法，与 v3 一致。

- [ ] **步骤 5：让协调者把新字段传下去**

`src/updater/coordinator_p.h` 第 23 行 `InstallerRequest::dataDirectory` 的注释
`optional bootstrap v3 restart context` 改为 `v4`，并增加握手请求结构：

```cpp
// GUI -> 协调者的握手请求。安装链路字段为空时退化为纯协议握手。
struct HandoffRequest {
    std::wstring dataDirectory;
    std::wstring installerPath;
    std::wstring installRoot;
    uint64_t packageSize=0;
    std::array<uint8_t,32> packageSha256{};
};
```

`Coordinator` 增加重载，**保留原签名**，现有测试调用点不必改：

```cpp
    bool start(const std::wstring& coordinator,const std::wstring& runtimeBase,
        const DirectoryIdentity* reservedIdentity=nullptr,
        const ProcessIdentity* application=nullptr,const std::wstring& dataDirectory={});
    // 携带安装链路字段的握手；其余语义与上面完全一致。
    bool start(const std::wstring& coordinator,const std::wstring& runtimeBase,
        const DirectoryIdentity* reservedIdentity,const ProcessIdentity* application,
        const HandoffRequest&);
```

`src/updater/coordinator.cpp` 把现有 `start` 的函数体整体移入新重载，组装
`ChildLaunchRequest` 时填入 `request` 的各字段；旧签名改为转发：

```cpp
bool Coordinator::start(const std::wstring& coordinator,const std::wstring& runtimeBase,
    const DirectoryIdentity* reservedIdentity,const ProcessIdentity* application,
    const std::wstring& dataDirectory){
    HandoffRequest request;request.dataDirectory=dataDirectory;
    return start(coordinator,runtimeBase,reservedIdentity,application,request);
}
```

- [ ] **步骤 6：推送并验证 Windows CI 通过**

```bash
git add src/updater/bootstrap_win_p.h src/updater/bootstrap_win.cpp \
  src/updater/coordinator_p.h src/updater/coordinator.cpp
git commit -m "feat: bootstrap v4 携带安装包路径、安装根与期望字节"
git push
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

预期：Windows job 全绿，`coordinator bootstrap v4` 全部断言通过。

---

### 任务 6：协调者生产中继（Windows）

`src/updater/main.cpp` 现在握手后直接发 `Failed` 并返回 `ExecutionDisabled`。它是唯一能
活过 GUI 退出的进程，因此 `proceedIfExited` 的放行判断必须在这里做。

**为什么要拆出 `relay.cpp`：** 生产二进制的启动器必须是写死的 `ShellExecuteEx runas`，
不能有任何注入面，否则它自己就成了提权漏洞。但这样一来真实二进制在 CI 上跑不了——
runner 弹不出 UAC。解法是把中继时序整体搬进 `relay.cpp`，由 `zzlogg_updater_handoff`
库导出；生产 `main.cpp` 只负责传入生产选项，测试侧另有一个替身入口传入 fixture 启动器。
两者执行的是**同一份**中继代码，生产源码里不存在任何编译期或运行期的注入开关。

**文件：**
- 创建：`src/updater/relay_p.h`、`src/updater/relay.cpp`
- 重写：`src/updater/main.cpp`
- 修改：`src/updater/CMakeLists.txt`、`tests/updater/CMakeLists.txt`
- 创建：`tests/updater/relayfixture.cpp`、`tests/updater/relaytest.cpp`

- [ ] **步骤 1：先读三份契约，再动手**

```bash
sed -n '1,60p' src/updater/handoffprotocol.h
sed -n '1,20p' src/updater/localchannel_win_p.h
sed -n '1,80p' src/updater/coordinator_p.h
```

要确认：`MessageKind` 的取值与线协议顺序、`after(DWORD)` 与 `Deadline` 的形状、
`CoordinationResult` 的全部取值、`CoordinatorOptions` 与 `InstallerLauncher` 的签名。

- [ ] **步骤 2：编写失败的测试**

创建 `tests/updater/relayfixture.cpp`——测试侧的中继入口，与生产入口**唯一**的区别是
注入的启动器：

```cpp
#include "bootstrap_win_p.h"
#include "relay_p.h"
using namespace zzlogg::updater::detail;
// 测试替身入口：中继逻辑是同一份 relay.cpp，只有启动器被换成 fixture。
// 生产 main.cpp 从不链接本文件。
int wmain(int argc,wchar_t** argv){
    ChildBootstrap bootstrap;
    if(!bootstrap.open(argc,argv))return BootstrapRejected;
    CoordinatorOptions options;
    options.requireElevatedPeer=false;
    options.launcher=[](const std::wstring& installer,const std::wstring& restrictedSwitch){
        // 与 tests/updater/coordinatortest.cpp 的 fixtureLaunch 同款：
        // 以普通权限启动 fixture，把受限开关原样传下去。
        return launchFixtureInstaller(installer,restrictedSwitch);
    };
    return runInstallRelay(bootstrap,options);
}
```

`launchFixtureInstaller` 直接把 `tests/updater/coordinatortest.cpp` 里 `fixtureLaunch`
的函数体搬过来，放在本文件的匿名命名空间里。

创建 `tests/updater/relaytest.cpp`。它以子进程方式驱动被测中继，覆盖三条不需要提权的
失败关闭路径，并在被测二进制是替身时走完整时序：

```cpp
#include "bootstrap_win_p.h"
#include "updatertesthelpers.h"
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
using namespace zzlogg::updater;
using namespace zzlogg::updater::detail;
// argv[1] = 被测中继（生产 ZzLoggUpdate.exe 或 zzlogg_updater_relayfixture.exe）
// argv[2] = 供 fixture 启动器扮演安装器/引擎的可执行文件
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    int failures=0;
    auto check=[&](bool ok,const char* name){
        if(!ok){++failures;std::cerr<<"FAIL: "<<name<<" error="<<GetLastError()<<'\n';}};
    const fs::path relay=argv[1],installer=argv[2];
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    const auto root=createTestRoot(temp);

    // 没有安装链路字段的 bootstrap 只是协议握手，没有可执行的安装。
    check(runRelay(relay,root,{})==ExecutionDisabled,
        "bootstrap without installer fields disables execution");

    // 声明的字节与磁盘上的实际内容不符：在启动任何安装器之前失败关闭。
    RelayRequest mismatched;
    mismatched.installerPath=installer;
    mismatched.installRoot=(root/L"install").wstring();
    mismatched.packageSize=fs::file_size(installer)+1;
    mismatched.packageSha256=fileSha256(installer);
    check(runRelay(relay,root,mismatched)==PackageRejected,
        "declared size mismatch is rejected before any launch");
    auto wrongDigest=mismatched;
    wrongDigest.packageSize=fs::file_size(installer);
    wrongDigest.packageSha256[0]^=1;
    check(runRelay(relay,root,wrongDigest)==PackageRejected,
        "declared digest mismatch is rejected before any launch");

    std::cout<<(failures?"relay FAILED":"relay ok")<<std::endl;
    return failures?1:0;
}
```

`createTestRoot` 来自 `tests/updater/updatertesthelpers.h`。`RelayRequest`、`runRelay`
与 `fileSha256` 是本测试新增的助手，写在同一文件里：`runRelay` 建立 bootstrap 映射、
以子进程方式启动被测中继、扮演 GUI 父端完成 `Hello/Ready`，然后等子进程退出并返回它的
退出码。映射构造照抄 `tests/updater/coordinatortest.cpp` 的 `spawnMappedFixture`，
通道服务端照抄同文件的 `ChainDrive`。

`tests/updater/CMakeLists.txt` 末尾注册替身与两条测试——同一份测试代码，分别打生产
二进制与替身二进制：

```cmake
add_executable(zzlogg_updater_relayfixture relayfixture.cpp)
target_link_libraries(zzlogg_updater_relayfixture PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(zzlogg_updater_relayfixture TRUE)
add_executable(zzlogg_updater_relay_test relaytest.cpp)
target_link_libraries(zzlogg_updater_relay_test PRIVATE zzlogg_updater_handoff)
zzlogg_update_runtime(zzlogg_updater_relay_test TRUE)
add_test(NAME zzlogg_updater.relay_production
  COMMAND zzlogg_updater_relay_test $<TARGET_FILE:ZzLoggUpdate>
    $<TARGET_FILE:zzlogg_updater_handofffixture>)
add_test(NAME zzlogg_updater.relay_fixture
  COMMAND zzlogg_updater_relay_test $<TARGET_FILE:zzlogg_updater_relayfixture>
    $<TARGET_FILE:zzlogg_updater_handofffixture>)
set_tests_properties(zzlogg_updater.relay_production zzlogg_updater.relay_fixture
  PROPERTIES TIMEOUT 120)
```

- [ ] **步骤 3：推分支跑 Windows CI 验证失败**

```bash
git add tests/updater/relaytest.cpp tests/updater/relayfixture.cpp tests/updater/CMakeLists.txt
git commit -m "test: 协调者中继的失败关闭路径"
git push && gh workflow run "CI Build" --ref <分支名>
```

预期：配置或编译失败，`relay_p.h` 与 `runInstallRelay` 都不存在。

- [ ] **步骤 4：实现中继**

创建 `src/updater/relay_p.h`：

```cpp
#pragma once
// 安装协调中继：ZzLoggUpdate.exe 活过 GUI 退出，因此 proceedIfExited 的放行
// 判断只能在这里做。生产入口（main.cpp）与测试替身入口执行同一份实现，
// 唯一的差别是传进来的 CoordinatorOptions。
#include "bootstrap_win_p.h"
#include "coordinator_p.h"
namespace zzlogg::updater::detail {
// 完成父端握手，复核安装包字节，驱动整条安装链路。返回进程退出码：
// 0 = 已安装并重启，其余见 bootstrap_win_p.h 的协调者退出码表。
int runInstallRelay(ChildBootstrap& bootstrap,const CoordinatorOptions& options);
}
```

创建 `src/updater/relay.cpp`：

```cpp
#include "relay_p.h"
#include "stablepackage_p.h"
#include <string>
namespace zzlogg::updater::detail {
namespace {
// UAC 提示与安装器解包都在这段时间里；引擎只有在用户接受提权之后才出现。
constexpr DWORD LaunchBudgetMs=300000;
// 引擎 prepare 是纯只读扫描，但要覆盖整个安装目录。
constexpr DWORD PrepareBudgetMs=180000;
constexpr DWORD ShortBudgetMs=5000;
// 事务执行加登记写入；超出即报失败而不是无界等待。
constexpr DWORD FinishBudgetMs=600000;
constexpr DWORD RestartBudgetMs=60000;

std::string hexDigest(const uint8_t (&digest)[32]) {
    static constexpr char hex[]="0123456789abcdef";
    std::string text;text.reserve(64);
    for(const auto byte:digest){text+=hex[byte>>4];text+=hex[byte&15];}
    return text;
}
// 协调者独立复核安装包字节：父进程的校验结论不构成依据。
bool packageUnchanged(const BootstrapData& data) {
    zzlogg::update::detail::StablePackage lease;
    if(lease.open(data.installerPath)!=zzlogg::update::PackageVerificationError::None)return false;
    return lease.verifyContent(data.packageSize,hexDigest(data.packageSha256))
        ==zzlogg::update::PackageVerificationError::None;
}
int fail(LocalChannel& channel,Message message,int code) {
    message.kind=MessageKind::Failed;channel.send(message,after(ShortBudgetMs));
    return code;
}
}
int runInstallRelay(ChildBootstrap& bootstrap,const CoordinatorOptions& options){
    LocalChannel channel;const auto deadline=after(3000);
    if(!channel.connect(bootstrap.data().transaction,bootstrap.parent(),deadline))return BootstrapRejected;
    Message message{MessageKind::Hello,bootstrap.data().transaction,bootstrap.data().token};
    if(!channel.send(message,deadline))return BootstrapRejected;
    const auto ready=channel.receive(deadline);
    if(!ready || ready->kind!=MessageKind::Ready || ready->transaction!=message.transaction
        || ready->token!=message.token || !bootstrap.parent().alive())return BootstrapRejected;
    const auto& data=bootstrap.data();
    if(!data.installerPath[0])return fail(channel,message,ExecutionDisabled);
    if(!packageUnchanged(data))return fail(channel,message,PackageRejected);
    Coordinator coordinator;
    const InstallerRequest request{data.installerPath,data.installRoot,data.dataDirectory};
    const DirectoryIdentity* reserved=bootstrap.directoryReserved()?&data.directory:nullptr;
    LaunchError launch=LaunchError::Failed;
    if(!coordinator.startInstaller(request,reserved,&bootstrap.parent(),options,&launch))
        return fail(channel,message,
            launch==LaunchError::Cancelled?ElevationDeclined:InstallerLaunchFailed);
    if(!coordinator.authenticate(after(LaunchBudgetMs)))return fail(channel,message,RelayFailed);
    if(coordinator.awaitAppExit(after(PrepareBudgetMs))!=CoordinationResult::WaitingForAppExit)
        return fail(channel,message,RelayFailed);
    // 引擎已就绪：这一刻才允许 GUI 提交退出。
    message.kind=MessageKind::AwaitingAppExit;
    if(!channel.send(message,after(ShortBudgetMs)))return fail(channel,message,RelayFailed);
    const auto commit=channel.receive(after(LaunchBudgetMs));
    if(!commit || commit->kind!=MessageKind::CommitExit)return fail(channel,message,RelayFailed);
    if(!coordinator.commitExit(after(ShortBudgetMs)))return fail(channel,message,RelayFailed);
    // GUI 进程真实退出之前绝不放行 Proceed。
    auto proceeded=CoordinationResult::PeerRunning;
    for(DWORD waited=0;waited<LaunchBudgetMs && proceeded==CoordinationResult::PeerRunning;waited+=50) {
        proceeded=coordinator.proceedIfExited(after(ShortBudgetMs));
        if(proceeded==CoordinationResult::PeerRunning)Sleep(50);
    }
    if(proceeded!=CoordinationResult::ProceedSent)return fail(channel,message,RelayFailed);
    const auto finished=coordinator.finish(after(FinishBudgetMs));
    if(finished!=CoordinationResult::Complete)
        return finished==CoordinationResult::ManualRestartRequired?RestartPending:RelayFailed;
    return coordinator.restart(after(RestartBudgetMs))==CoordinationResult::Restarted
        ?0:RestartPending;
}
}
```

`src/updater/main.cpp` 整体替换为生产入口：

```cpp
#include "bootstrap_win_p.h"
#include "relay_p.h"
using namespace zzlogg::updater::detail;
int wmain(int argc,wchar_t** argv){
    ChildBootstrap bootstrap;
    if(!bootstrap.open(argc,argv))return BootstrapRejected;
    // 生产选项：空 launcher 即写死的 ShellExecuteEx runas，并要求提权对端。
    // 这里没有、也绝不能有任何可注入的启动器。
    return runInstallRelay(bootstrap,CoordinatorOptions{});
}
```

`src/updater/CMakeLists.txt` 的 `zzlogg_add_updater_handoff` 源文件列表末尾加入
`relay.cpp`（该函数已经带了 `target_include_directories(... ../update/src)`，
`stablepackage_p.h` 可直接包含）。

- [ ] **步骤 5：扩展测试覆盖完整时序**

`tests/updater/relaytest.cpp` 在三条失败关闭用例之后追加——只对替身执行，生产二进制
会真的弹 UAC：

```cpp
    if(relay.stem()==L"zzlogg_updater_relayfixture") {
        // 完整中继时序：引擎就绪才转发 AwaitingAppExit，GUI 真实退出才放行 Proceed。
        RelayRequest good;
        good.installerPath=installer;
        good.installRoot=(root/L"install").wstring();
        good.packageSize=fs::file_size(installer);
        good.packageSha256=fileSha256(installer);
        RelayObservation observed;
        check(runRelayChain(relay,root,good,observed)==0,"fixture relay completes the chain");
        check(observed.awaitingBeforeCommit,
            "AwaitingAppExit reached the GUI only after the engine was waiting");
        check(observed.proceedAfterApplicationExit,
            "Proceed was released only after the application process really exited");
    }
```

`RelayObservation` 与 `runRelayChain` 写在同一文件：父端记录收到 `AwaitingAppExit`
的时刻，然后发 `CommitExit` 并真实结束扮演应用的进程，再从 fixture 的记录文件
（`ZZLOGG_HANDOFF_RECORD`，见 `tests/updater/handofffixture.cpp:16`）读回引擎侧观察到的
消息顺序。

- [ ] **步骤 6：推送并验证 Windows CI 通过**

```bash
git add src/updater/relay_p.h src/updater/relay.cpp src/updater/main.cpp \
  src/updater/CMakeLists.txt tests/updater/relaytest.cpp tests/updater/relayfixture.cpp \
  tests/updater/CMakeLists.txt
git commit -m "feat: ZzLoggUpdate 组合生产安装协调中继"
git push
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

预期：Windows job 全绿，`relay_production` 与 `relay_fixture` 都通过。

- [ ] **步骤 7：确认生产闸测试仍然成立**

`tests/updater/productiongatetest.cmake` 断言裸启动 `ZzLoggUpdate.exe` 返回 41。中继
没有改变"无 bootstrap 即 `BootstrapRejected`"这条路径，但必须实测确认：

```bash
gh run view --log "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')" | rg 'production_gate'
```

预期：PASS。若失败，是 `wmain` 的早退顺序被改动，回步骤 4 修正。

---

### 任务 7：生产会话工厂（Windows）

`ApplicationUpdateHandoff` 的生产构造函数不带工厂，`executionAvailable()` 恒为 false
（`src/app/applicationupdatehandoff.cpp:123-128`），这就是界面显示"此版本尚未启用安装
更新"的直接原因。

**文件：**
- 创建：`src/app/applicationupdatesession.h`、`src/app/applicationupdatesession.cpp`
- 修改：`src/app/applicationupdatehandoff.h`、`src/app/applicationupdatehandoff.cpp`
- 修改：`src/app/kloggapp.h`、`src/app/CMakeLists.txt`
- 测试：`tests/ui_acceptance/updatehandofftest.cpp`

- [ ] **步骤 1：编写失败的测试——工厂开闸条件矩阵**

`tests/ui_acceptance/updatehandofftest.cpp` 新增用例，只断言开闸判断，不驱动真实安装：

```cpp
    void productionFactoryOpensOnlyWithEveryCondition() {
        ApplicationUpdateSessionInputs inputs;
        inputs.registeredInstallRoot="C:\\Program Files\\ZzLogg";
        inputs.verifiedPackagePath="C:\\Users\\u\\AppData\\Local\\ZzLogg\\cache\\setup.exe";
        inputs.packageSize=1024;
        inputs.packageSha256=QString(64,'a');
        inputs.releaseIdentityAvailable=true;
        // 每个条件单独取反都必须关闸；只有全部成立才开。
        auto missingRoot=inputs; missingRoot.registeredInstallRoot.clear();
        QVERIFY(makeUpdateSessionFactory(missingRoot)==nullptr);
        auto missingPackage=inputs; missingPackage.verifiedPackagePath.clear();
        QVERIFY(makeUpdateSessionFactory(missingPackage)==nullptr);
        auto missingSize=inputs; missingSize.packageSize=0;
        QVERIFY(makeUpdateSessionFactory(missingSize)==nullptr);
        auto shortDigest=inputs; shortDigest.packageSha256="abc";
        QVERIFY(makeUpdateSessionFactory(shortDigest)==nullptr);
        auto upperDigest=inputs; upperDigest.packageSha256=QString(64,'A');
        QVERIFY(makeUpdateSessionFactory(upperDigest)==nullptr);
        auto unofficial=inputs; unofficial.releaseIdentityAvailable=false;
        QVERIFY(makeUpdateSessionFactory(unofficial)==nullptr);
#ifdef Q_OS_WIN
        QVERIFY(makeUpdateSessionFactory(inputs)!=nullptr);
#else
        // 非 Windows 目标上即便条件齐备也没有可组合的协调者。
        QVERIFY(makeUpdateSessionFactory(inputs)==nullptr);
#endif
    }
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build /你的构建目录 --target zzlogg_update_handoff_test
```

预期：FAIL，编译期报找不到 `ApplicationUpdateSessionInputs`。这一条在 Linux 上就能验证。

- [ ] **步骤 3：扩展 Request 与 offer，让会话拿得到安装根与期望字节**

`src/app/applicationupdatehandoff.h` 的 `Request` 增加三个字段：

```cpp
    struct Request {
        std::optional<zzlogg::updater::DirectoryIdentity> directory;
        QString packagePath;
        QString releaseVersion;
        // 登记安装目录，以及签名清单给出的期望字节。协调者会独立复核，
        // 这里传递的只是数据，不构成执行授权。
        QString installRoot;
        quint64 packageSize=0;
        QString packageSha256;
    };
```

`src/app/kloggapp.h` 的 `verifiedUpdateOffer()` 返回类型从 `std::pair<QString,QString>`
改为具名结构，避免用位置元组承载五个值：

```cpp
    struct VerifiedUpdateOffer {
        QString packagePath;
        QString releaseVersion;
        QString installRoot;
        quint64 packageSize=0;
        QString packageSha256;
    };
```

`installRoot` 取 `zzlogg::updateqt::probeCurrentInstallation().installRoot`，
`packageSize` 与 `packageSha256` 取下载快照 `selection->artifact` 的对应字段。
`src/app/applicationupdatehandoff.cpp:190-192` 相应改为逐字段赋值。

- [ ] **步骤 4：实现会话与工厂**

创建 `src/app/applicationupdatesession.h`：

```cpp
#ifndef KLOGG_APPLICATIONUPDATESESSION_H
#define KLOGG_APPLICATIONUPDATESESSION_H

#include "applicationupdatehandoff.h"

// 生产会话工厂的开闸输入。全部条件成立才返回非空工厂；任意一项缺失都让
// executionAvailable() 保持 false，界面不出现"退出并安装更新"。
struct ApplicationUpdateSessionInputs {
    QString registeredInstallRoot;
    QString verifiedPackagePath;
    quint64 packageSize = 0;
    QString packageSha256; // 64 位小写十六进制
    bool releaseIdentityAvailable = false;
};

// 非 Windows 目标、或任一条件不成立时返回空工厂。
ApplicationUpdateHandoff::SessionFactory makeUpdateSessionFactory(
    const ApplicationUpdateSessionInputs&);

#endif // KLOGG_APPLICATIONUPDATESESSION_H
```

创建 `src/app/applicationupdatesession.cpp`。开闸判断是跨平台纯逻辑，会话实现在
`#ifdef Q_OS_WIN` 内：

```cpp
#include "applicationupdatesession.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include "coordinator_p.h"
#include "storagecontext.h"
#endif

namespace {
bool lowercaseHexDigest(const QString& value)
{
    if (value.size() != 64) return false;
    for (const QChar character : value)
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return false;
    return true;
}

#ifdef Q_OS_WIN
namespace updater = zzlogg::updater::detail;

// Coordinator 到 CoordinationSession 的适配。每个方法都在交接控制器的 worker
// 线程上被调用，内部等待全部有界。
class CoordinatorSession final : public ApplicationUpdateHandoff::CoordinationSession {
  public:
    CoordinatorSession(std::wstring image, std::wstring base, updater::HandoffRequest request,
                       std::optional<zzlogg::updater::DirectoryIdentity> reserved)
        : image_(std::move(image)), base_(std::move(base)), request_(std::move(request)),
          reserved_(reserved) {}

    ApplicationUpdateHandoff::Failure start() override
    {
        const auto* reserved = reserved_ ? &*reserved_ : nullptr;
        if (!coordinator_.start(image_, base_, reserved, nullptr, request_))
            return ApplicationUpdateHandoff::Failure::LaunchFailed;
        if (!coordinator_.authenticate(updater::after(10000)))
            return ApplicationUpdateHandoff::Failure::PeerRejected;
        // 协调者只有在它自己的引擎进入等待之后才转发 AwaitingAppExit；这段时间
        // 覆盖 UAC 提示与安装器解包，所以预算按分钟计。
        const auto waiting = coordinator_.awaitAppExit(updater::after(300000));
        if (waiting == updater::CoordinationResult::WaitingForAppExit)
            return ApplicationUpdateHandoff::Failure::None;
        // 线协议没有原因字段：协调者退出码是区分"用户拒绝提权"与其他失败的
        // 唯一依据，否则 UAC 点"否"会被显示成"更新助手无法启动"。
        return failureFromChildExit();
    }
    bool canCommitExit() const override { return coordinator_.canCommitExit(); }
    bool commitExit() override { return coordinator_.commitExit(updater::after(5000)); }
    void cancel() override { coordinator_.cancel(updater::after(5000)); }

  private:
    ApplicationUpdateHandoff::Failure failureFromChildExit() const
    {
        DWORD code = 0;
        const auto handle = coordinator_.process().handle();
        if (handle && WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0
            && GetExitCodeProcess(handle, &code)) {
            if (code == static_cast<DWORD>(updater::ElevationDeclined))
                return ApplicationUpdateHandoff::Failure::ApprovalDeclined;
            if (code == static_cast<DWORD>(updater::InstallerLaunchFailed))
                return ApplicationUpdateHandoff::Failure::LaunchFailed;
        }
        return ApplicationUpdateHandoff::Failure::PeerLost;
    }

    updater::Coordinator coordinator_;
    std::wstring image_, base_;
    updater::HandoffRequest request_;
    std::optional<zzlogg::updater::DirectoryIdentity> reserved_;
};
#endif
} // namespace

ApplicationUpdateHandoff::SessionFactory makeUpdateSessionFactory(
    const ApplicationUpdateSessionInputs& inputs)
{
    if (!inputs.releaseIdentityAvailable || inputs.registeredInstallRoot.isEmpty()
        || inputs.verifiedPackagePath.isEmpty() || inputs.packageSize == 0
        || !lowercaseHexDigest(inputs.packageSha256))
        return {};
#ifdef Q_OS_WIN
    return [](const ApplicationUpdateHandoff::Request& request)
               -> std::unique_ptr<ApplicationUpdateHandoff::CoordinationSession> {
        // 协调者镜像随应用安装；运行时副本放在用户缓存下。两者都是普通用户
        // 权限，不承载任何安装授权。
        const auto image = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ZzLoggUpdate.exe"));
        const auto base = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                              .filePath(QStringLiteral("updates/production/runtime-v1"));
        if (!QFileInfo::exists(image) || !QDir().mkpath(base)) return nullptr;
        const auto digest = QByteArray::fromHex(request.packageSha256.toLatin1());
        if (digest.size() != 32) return nullptr;
        updater::HandoffRequest native;
        native.dataDirectory
            = QDir::toNativeSeparators(StorageContext::current().runtimePaths().appConfigDirectory)
                  .toStdWString();
        native.installerPath = QDir::toNativeSeparators(request.packagePath).toStdWString();
        native.installRoot = QDir::toNativeSeparators(request.installRoot).toStdWString();
        native.packageSize = request.packageSize;
        std::copy(digest.begin(), digest.end(), native.packageSha256.begin());
        return std::make_unique<CoordinatorSession>(
            QDir::toNativeSeparators(image).toStdWString(),
            QDir::toNativeSeparators(base).toStdWString(), std::move(native), request.directory);
    };
#else
    return {};
#endif
}
```

`src/app/CMakeLists.txt` 第 56-57 行附近加入这两个新文件。

- [ ] **步骤 5：在 KloggApp 中传入工厂**

`updateHandoff()` 现在是惰性单例，而开闸条件随下载状态变化，所以工厂不能只算一次。
两处改动：

`pushUpdateExecutionCapability()` 改为直接按当前条件判断，不再问 `updateHandoff()`：

```cpp
    ApplicationUpdateSessionInputs updateSessionInputs() const
    {
        const auto offer=verifiedUpdateOffer();
        ApplicationUpdateSessionInputs inputs;
        inputs.registeredInstallRoot=offer.installRoot;
        inputs.verifiedPackagePath=offer.packagePath;
        inputs.packageSize=offer.packageSize;
        inputs.packageSha256=offer.packageSha256;
        inputs.releaseIdentityAvailable=zzlogg::update::compiledReleaseIdentity().has_value();
        return inputs;
    }

    void pushUpdateExecutionCapability()
    {
        if ( !updateDialog_ ) return;
        updateDialog_->setUpdateExecutionAvailable(
            makeUpdateSessionFactory( updateSessionInputs() ) != nullptr );
    }
```

`installRequested` 的处理改为每次用新鲜的工厂重建控制器，避免陈旧工厂锁死能力：

```cpp
            connect(dialog,&UpdateCheckDialog::installRequested,this,[this] {
                if(updateHandoff_ && !updateHandoff_->isWaiting()) updateHandoff_.reset();
                updateHandoff().begin();
            });
```

`updateHandoff()` 内部构造改为：

```cpp
            auto handoff=std::make_unique<ApplicationUpdateHandoff>(
                *this,makeUpdateSessionFactory(updateSessionInputs()));
```

- [ ] **步骤 6：本机与 Windows CI 双向验证**

```bash
cmake --build /你的构建目录 --target ci_build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 --output-on-failure -j4
git add src/app/applicationupdatesession.h src/app/applicationupdatesession.cpp \
  src/app/applicationupdatehandoff.h src/app/applicationupdatehandoff.cpp \
  src/app/kloggapp.h src/app/CMakeLists.txt tests/ui_acceptance/updatehandofftest.cpp
git commit -m "feat: 接入生产更新会话工厂"
git push
gh run watch "$(gh run list --workflow 'CI Build' --branch <分支名> --limit 1 --json databaseId --jq '.[0].databaseId')"
```

预期：Linux 全绿（工厂恒空，既有断言
`QVERIFY(!handoff.executionAvailable())`（`tests/ui_acceptance/updatehandofftest.cpp:357`）
仍然成立），Windows 全绿。

---

### 任务 8：界面文案修正

`updatecheckdialog.cpp:170-181` 的 `else` 兜底让"此版本尚未启用安装更新"在"已是最新版"
时也会显示，这正是本次问题的起因。

**文件：**
- 修改：`src/ui/src/updatecheckdialog.cpp`
- 测试：`tests/ui_acceptance/updatecheckuitest.cpp`

- [ ] **步骤 1：编写失败的测试**

在 `tests/ui_acceptance/updatecheckuitest.cpp` 的 `updateHintsReflectActualCapability`
之后新增：

```cpp
    void upToDateNeverMentionsInstallCapability() {
        UpdateCheckDialog dialog;
        auto* hint = dialog.findChild<QLabel*>("updateHint");
        QVERIFY(hint);
        dialog.setSnapshot({CheckStatus::UpToDate, Channel::Stable, {}, {}, true});
        // 已是最新版时谈论安装能力，会被读成"发现了新版但装不了"。
        QVERIFY(hint->text().isEmpty());
        dialog.setSnapshot({CheckStatus::Idle, Channel::Stable, {}, {}, true});
        QVERIFY(hint->text().isEmpty());
    }
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build /你的构建目录 --target zzlogg_update_check_ui_test
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 -R 'update_check_ui' --output-on-failure
```

预期：FAIL，`hint->text()` 是 "Update installation is not enabled for this build..."。

- [ ] **步骤 3：实现**

`src/ui/src/updatecheckdialog.cpp` 的提示分支替换为：

```cpp
    if(snapshot_.status==CheckStatus::NotConfigured)
        hint_->setText(tr("Online updates are not configured for this build. Download releases from GitHub."));
    else if(snapshot_.status==CheckStatus::ReleaseInformation || snapshot_.status==CheckStatus::Unsupported
        || snapshot_.status==CheckStatus::ManualUpdateAvailable)
        hint_->setText(tr("Automatic installation is unavailable for this installation. Download a compatible package from GitHub."));
    else if(installAvailable())
        hint_->setText(tr("The update is verified. Choose Quit and install update to continue."));
    else if(updateExecutionAvailable_)
        hint_->setText(tr("Download and verify the update before installing it."));
    else if(snapshot_.status==CheckStatus::Available)
        // 只有真的发现了新版本却装不了，才谈安装能力。
        hint_->setText(tr("Update installation is not enabled for this build. Download releases from GitHub."));
    else
        hint_->clear();
```

- [ ] **步骤 4：运行测试验证通过**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 -R 'update_check_ui' --output-on-failure
```

预期：PASS。原有的 `updateHintsReflectActualCapability` 不受影响——它用
`availableRelease()`，状态就是 `Available`。

- [ ] **步骤 5：确认翻译无需改动并 commit**

本次没有新增或修改 `tr()` 字符串，只是分支条件调整，`.ts` 文件应当无变化：

```bash
git status --short src/app/i18n
```

预期：无输出。若有输出，说明误改了字符串，回步骤 3 核对。

```bash
git add src/ui/src/updatecheckdialog.cpp tests/ui_acceptance/updatecheckuitest.cpp
git commit -m "fix: 已是最新版时不再提示安装更新未启用"
```

---

### 任务 9：文档与变更记录

**文件：**
- 修改：`docs/development/GITHUB_UPDATES.md`、`docs/development/UPDATE_PROTOCOL.md`、`docs/development/UPDATE_ACCEPTANCE_VM.md`、`CHANGELOG.md`

- [ ] **步骤 1：改写 GITHUB_UPDATES.md 的两处过期结论**

第 5 行"目前没有 Windows 代码签名证书……本次没有放宽安装包校验"整段替换为：

```markdown
项目没有 Windows 代码签名证书。执行安装的信任根是 Ed25519 签名清单给出的字节大小与
SHA-256：清单本身经过签名验证、有效期检查、渠道绑定与防重放，安装包必须逐字节匹配才被
授权执行。发布者指纹列表留空，将来取得证书后填入即自动叠加 Authenticode 校验。已知限制：
安装时 Windows UAC 显示"未知发布者"。
```

第 47 行"Windows 一键安装是后续独立工作……"整段替换为：

```markdown
Windows 一键安装从 v26.09.03 起在稳定版渠道可用，限 x64 注册安装。预览渠道继续使用发布页
手动下载：预览构建常共用同一显示版本，客户端不把同版本的新每日构建判为可升级。
```

- [ ] **步骤 2：更新 UPDATE_PROTOCOL.md**

`### bootstrap v3`（第 411 行）改名为 `### bootstrap v4`，补充 `installerPath`、
`installRoot`、`packageSize` 与 `packageSha256` 的容量、校验规则与"整组齐备或整组缺席"
约束，并记录协调者退出码表（60–64）与事务引擎码位（42–48）不重叠。

`### 3C 未交付项与阶段 4 真实环境验收清单`（第 535 行）的"仍未交付"段落删去已交付的
三项——应用内生产会话工厂、生产发布者证书指纹允许列表（改为记录清单锚定裁决）、生产
HTTPS 地址与公钥配置——其余真实环境验收条目保留不动。

- [ ] **步骤 3：更新验收手册**

`docs/development/UPDATE_ACCEPTANCE_VM.md` 增加一节"测试源预演"，说明在等待 v26.09.04
之前如何用 `tools/update/testfeed.cpp` 以假清单在 VM 上跑通整条链路，并把下列四条列为
本次必做用例：真实 UAC 接受、真实 UAC 拒绝、替换事务中断恢复、装完自动重启与启动确认。

- [ ] **步骤 4：更新 CHANGELOG**

在 `## [Unreleased]` 下加入：

```markdown
### 新增

- Windows 稳定版支持在应用内一键安装更新：下载校验通过后可直接退出并完成升级，
  失败自动回滚。安装时系统会提示未知发布者，项目暂无代码签名证书。
```

- [ ] **步骤 5：让冒烟流水线覆盖新的触发面**

`.github/workflows/update-smoke.yml` 的 `paths:` 现在只含 `src/updater/**` 等四项，
本次把包校验与会话工厂也拉进了安装链路，它们变更时同样需要真实安装冒烟：

```yaml
    paths:
      - 'src/updater/**'
      - 'src/update/**'
      - 'src/app/applicationupdatesession.cpp'
      - 'packaging/windows/**'
      - '.github/actions/agent-package-win/**'
      - 'tools/acceptance/**'
      - '.github/workflows/update-smoke.yml'
```

- [ ] **步骤 6：Commit**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/update-smoke.yml')); print('ok')"
git add docs/development/GITHUB_UPDATES.md docs/development/UPDATE_PROTOCOL.md \
  docs/development/UPDATE_ACCEPTANCE_VM.md CHANGELOG.md .github/workflows/update-smoke.yml
git commit -m "docs: 记录 Windows 一键安装上线与清单锚定信任根"
```

---

## 完成后的人工验收

以下工作无法由代理完成，需要在 Windows 机器上执行。CI 不能弹 UAC，生产二进制也不接受
注入启动器，所以真实提权之后的整条链路只有这里才被真正验证过：

1. 用 `tools/update/testfeed.cpp` 搭本地测试源，在干净 VM 上跑完整链路：下载、校验、
   UAC 接受、事务替换、自动重启。
2. 重复一次并在 UAC 提示上点"否"，确认应用保持运行、会话未变、预留已释放，且界面显示
   的是"管理员授权被拒绝"而不是"更新助手无法启动"。
3. 在事务执行中途结束引擎进程，确认安装目录回到事务前状态。
4. 发布 v26.09.03 后手动安装一次；发布 v26.09.04 时做首次真实的端到端一键安装。
