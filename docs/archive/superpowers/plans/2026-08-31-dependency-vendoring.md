# ZzPureTools 更新与 backward-cpp 离线化实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 ZzPureTools 固定到提交 `f9e6c6f`，并把 backward-cpp v1.6 从子模块转换为主仓库直接跟踪、无需联网初始化的 vendored 源码。

**架构：** ZzPureTools 继续保留为独立子模块，只更新 gitlink；backward-cpp 保持现有源码路径和 CMake `add_subdirectory` 接口，但其 24 个上游文件改由 ZzLogg 主仓库直接跟踪。新增一个 CMake/CTest 依赖布局契约，使用 Git 索引和静态文件内容同时锁定 ZzPureTools 提交、backward-cpp 非 gitlink 状态、上游来源和本地接入方式。

**技术栈：** Git submodule/gitlink、CMake 4、CTest、PowerShell、Qt 6.11、MSVC 19.51、backward-cpp v1.6、ZzPureTools。

---

## 文件结构

- 修改：`CMakeLists.txt` — 注册依赖布局契约测试并传入 Git 可执行文件。
- 创建：`cmake/VerifyDependencyLayout.cmake` — 验证两个第三方依赖的索引模式、固定版本、来源文件和本地 CMake 接入方式。
- 修改：`3rdparty/vendor/ZzPureTools` — 将 gitlink 从 `f8f60ba` 更新到 `f9e6c6f`。
- 修改：`.gitmodules` — 删除 backward-cpp 子模块声明，只保留 ZzPureTools。
- 修改：`3rdparty/CMakeLists.txt` — 在添加 backward-cpp 前验证 vendored 源码完整性，并保持纯本地 `add_subdirectory`。
- 转换：`3rdparty/vendor/backward-cpp` — 从 mode `160000` gitlink 转为上游 v1.6 的 24 个普通文件。
- 创建：`3rdparty/vendor/backward-cpp/UPSTREAM.md` — 记录上游地址、版本、提交和 MIT 许可证。
- 修改：`README.md` — 说明 ZzPureTools 是剩余子模块、backward-cpp 已随仓库提供。
- 修改：`docs/BUILD.md` — 说明子模块初始化边界和 backward-cpp 离线范围。

### 任务 1：更新并锁定 ZzPureTools

**文件：**
- 创建：`cmake/VerifyDependencyLayout.cmake`
- 修改：`CMakeLists.txt:240-250`
- 修改：`3rdparty/vendor/ZzPureTools`（gitlink）

- [ ] **步骤 1：编写只检查 ZzPureTools 固定提交的失败契约**

创建 `cmake/VerifyDependencyLayout.cmake`：

```cmake
if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED GIT_EXECUTABLE OR NOT EXISTS "${GIT_EXECUTABLE}")
  message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

set(expected_zzpuretools_commit
  "f9e6c6f25af8061666cc04da2a43c0a1d3cfe271")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" ls-files -s --
          "3rdparty/vendor/ZzPureTools"
  RESULT_VARIABLE zzpuretools_index_result
  OUTPUT_VARIABLE zzpuretools_index
  ERROR_VARIABLE zzpuretools_index_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT zzpuretools_index_result EQUAL 0)
  message(FATAL_ERROR
    "Unable to inspect ZzPureTools gitlink: ${zzpuretools_index_error}")
endif()
if(NOT zzpuretools_index MATCHES
   "^160000 ${expected_zzpuretools_commit} 0")
  message(FATAL_ERROR
    "ZzPureTools gitlink is not pinned to ${expected_zzpuretools_commit}: ${zzpuretools_index}")
endif()

message(STATUS "Dependency layout contract passed")
```

在 `CMakeLists.txt` 的 `enable_testing()` 后、现有品牌契约前注册：

```cmake
find_program(ZZLOGG_GIT_EXECUTABLE NAMES git REQUIRED)
add_test(
  NAME zzlogg_ui2.dependency_layout
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DGIT_EXECUTABLE=${ZZLOGG_GIT_EXECUTABLE}"
    -P "${PROJECT_SOURCE_DIR}/cmake/VerifyDependencyLayout.cmake"
)
```

- [ ] **步骤 2：直接运行契约并确认旧 gitlink 失败**

运行：

```powershell
$gitExe = (Get-Command git).Source
& 'D:\SoftWare\CMake\bin\cmake.exe' `
  "-DSOURCE_ROOT=$((Resolve-Path '.').Path)" `
  "-DGIT_EXECUTABLE=$gitExe" `
  -P cmake/VerifyDependencyLayout.cmake
```

预期：退出码非 0，错误包含
`ZzPureTools gitlink is not pinned to f9e6c6f25af8061666cc04da2a43c0a1d3cfe271`，并显示当前 `f8f60bae...`。

- [ ] **步骤 3：将子模块工作树切到已审核的固定提交**

运行：

```powershell
git -C 3rdparty/vendor/ZzPureTools fetch origin master
git -C 3rdparty/vendor/ZzPureTools checkout --detach `
  f9e6c6f25af8061666cc04da2a43c0a1d3cfe271
git add -- 3rdparty/vendor/ZzPureTools
git diff --cached --submodule=log -- 3rdparty/vendor/ZzPureTools
```

预期：只有 ZzPureTools gitlink 从 `f8f60ba` 移动到 `f9e6c6f`；子模块工作树没有额外修改。

- [ ] **步骤 4：重新运行依赖契约并确认通过**

运行步骤 2 的相同命令。

预期：退出码 0，输出包含 `Dependency layout contract passed`。

- [ ] **步骤 5：fresh 配置、构建并运行 Debug 全量测试**

运行：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
$env:QT_QPA_PLATFORM = 'offscreen'
$env:APPDATA = "$((Resolve-Path '.').Path)\out\verify-zzpuretools\AppData"
$env:XDG_CONFIG_HOME = "$((Resolve-Path '.').Path)\out\verify-zzpuretools\xdg"
New-Item -ItemType Directory -Force -Path $env:APPDATA,$env:XDG_CONFIG_HOME | Out-Null
& 'D:\SoftWare\CMake\bin\cmake.exe' --fresh --preset windows-vs2026-ui2 `
  -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui2-vs -C Debug --output-on-failure
```

预期：配置和构建退出码 0；完整 CTest 为 21/21。根据已检查的 21 个上游提交，公开接口只有新增项，没有删除 ZzLogg 当前调用的接口，因此本任务不预设兼容源码修改。若实际编译结果与此前提不一致，保存完整失败输出并停止该任务，先修订本计划后再改调用代码。

- [ ] **步骤 6：提交 ZzPureTools 更新**

运行：

```powershell
git diff --check
git status --short
git add -- CMakeLists.txt cmake/VerifyDependencyLayout.cmake `
  3rdparty/vendor/ZzPureTools
git diff --cached --check
git commit -m "build: 更新 ZzPureTools 依赖"
```

预期：提交只包含契约、测试注册和 gitlink。

### 任务 2：将 backward-cpp 转换为普通 vendored 源码

**文件：**
- 修改：`cmake/VerifyDependencyLayout.cmake`
- 修改：`.gitmodules`
- 修改：`3rdparty/CMakeLists.txt:78-83`
- 转换：`3rdparty/vendor/backward-cpp`（gitlink → 普通文件树）
- 创建：`3rdparty/vendor/backward-cpp/UPSTREAM.md`
- 修改：`README.md:30-40`
- 修改：`docs/BUILD.md:3-20`

- [ ] **步骤 1：扩展契约，使当前 backward-cpp 子模块布局失败**

在 `cmake/VerifyDependencyLayout.cmake` 的 ZzPureTools 检查之后加入：

```cmake
file(READ "${SOURCE_ROOT}/.gitmodules" gitmodules_content)
if(gitmodules_content MATCHES "3rdparty/vendor/backward-cpp|bombela/backward-cpp")
  message(FATAL_ERROR "backward-cpp must not remain in .gitmodules")
endif()
if(NOT gitmodules_content MATCHES "3rdparty/vendor/ZzPureTools")
  message(FATAL_ERROR "ZzPureTools submodule declaration is missing")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" ls-files -s --
          "3rdparty/vendor/backward-cpp"
  RESULT_VARIABLE backward_index_result
  OUTPUT_VARIABLE backward_index
  ERROR_VARIABLE backward_index_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT backward_index_result EQUAL 0)
  message(FATAL_ERROR "Unable to inspect backward-cpp files: ${backward_index_error}")
endif()
if(backward_index STREQUAL "")
  message(FATAL_ERROR "backward-cpp vendored files are not tracked")
endif()
if(backward_index MATCHES "(^|\n)160000 ")
  message(FATAL_ERROR "backward-cpp is still a gitlink")
endif()

set(backward_required_files
  CMakeLists.txt
  BackwardConfig.cmake
  backward.cpp
  backward.hpp
  LICENSE.txt
  README.md
  UPSTREAM.md)
foreach(backward_file IN LISTS backward_required_files)
  if(NOT EXISTS "${SOURCE_ROOT}/3rdparty/vendor/backward-cpp/${backward_file}")
    message(FATAL_ERROR "backward-cpp vendored file is missing: ${backward_file}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/3rdparty/vendor/backward-cpp/UPSTREAM.md"
  backward_upstream)
foreach(required_upstream_literal IN ITEMS
    "https://github.com/bombela/backward-cpp"
    "v1.6"
    "3bb9240cb15459768adb3e7d963a20e1523a6294"
    "MIT")
  string(FIND "${backward_upstream}" "${required_upstream_literal}"
    upstream_position)
  if(upstream_position EQUAL -1)
    message(FATAL_ERROR
      "backward-cpp provenance omits: ${required_upstream_literal}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/3rdparty/CMakeLists.txt" thirdparty_cmake)
string(REGEX MATCHALL
  "add_subdirectory\\([ \t\r\n]*vendor/backward-cpp[ \t\r\n]*\\)"
  backward_local_additions "${thirdparty_cmake}")
list(LENGTH backward_local_additions backward_local_addition_count)
if(NOT backward_local_addition_count EQUAL 1)
  message(FATAL_ERROR
    "backward-cpp must be added exactly once from its vendored directory")
endif()
if(thirdparty_cmake MATCHES
   "(FetchContent_Declare|CPMAddPackage)[^\n]*[Bb]ackward")
  message(FATAL_ERROR "backward-cpp has a network download fallback")
endif()
string(FIND "${thirdparty_cmake}"
  "Vendored backward-cpp is incomplete: missing"
  backward_guard_position)
if(backward_guard_position EQUAL -1)
  message(FATAL_ERROR "backward-cpp vendored completeness guard is missing")
endif()
```

- [ ] **步骤 2：运行扩展契约并确认 gitlink 被拒绝**

运行任务 1 步骤 2 的直接 CMake 脚本命令。

预期：退出码非 0，首先报告 `backward-cpp must not remain in .gitmodules`；移除该声明后，旧布局还会报告 `backward-cpp is still a gitlink`。

- [ ] **步骤 3：验证精确目标并解除 backward-cpp 的 gitlink 元数据**

运行：

```powershell
$workspace = [IO.Path]::GetFullPath((git rev-parse --show-toplevel))
$backwardRoot = [IO.Path]::GetFullPath((Resolve-Path '3rdparty/vendor/backward-cpp'))
$backwardGitFile = [IO.Path]::GetFullPath((Resolve-Path '3rdparty/vendor/backward-cpp/.git'))
if (-not $backwardRoot.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) { throw 'backward-cpp is outside the worktree' }
if ((Get-Item -Force -LiteralPath $backwardGitFile).PSIsContainer) {
  throw 'Expected a submodule gitfile, not a .git directory'
}
if ((git -C 3rdparty/vendor/backward-cpp rev-parse HEAD) -ne
    '3bb9240cb15459768adb3e7d963a20e1523a6294') {
  throw 'backward-cpp is not at the approved v1.6 commit'
}
git rm --cached -- 3rdparty/vendor/backward-cpp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
[IO.File]::Delete($backwardGitFile)
if (Test-Path -LiteralPath $backwardGitFile) { throw 'submodule gitfile was not removed' }
```

该操作只删除已核验路径中的 `.git` 指针文件；上游对象库仍保留在主仓库 worktree 的 Git 元数据中，24 个源码文件不删除。

- [ ] **步骤 4：删除子模块声明并增加上游来源文件**

把 `.gitmodules` 收敛为：

```ini
[submodule "3rdparty/vendor/ZzPureTools"]
	path = 3rdparty/vendor/ZzPureTools
	url = https://github.com/jackfahdin/ZzPureTools.git
```

创建 `3rdparty/vendor/backward-cpp/UPSTREAM.md`：

```markdown
# backward-cpp upstream provenance

- Project: https://github.com/bombela/backward-cpp
- Version: v1.6
- Commit: `3bb9240cb15459768adb3e7d963a20e1523a6294`
- License: MIT; see `LICENSE.txt`

This directory is a source snapshot tracked directly by ZzLogg so that
backward-cpp does not require submodule initialization or a configure-time
download.
```

把 `3rdparty/CMakeLists.txt` 中现有 backward-cpp 两行替换为：

```cmake
  # backward-cpp
  set(backward_cpp_source_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/backward-cpp")
  foreach(backward_cpp_file IN ITEMS
      CMakeLists.txt BackwardConfig.cmake backward.cpp backward.hpp LICENSE.txt)
    if(NOT EXISTS "${backward_cpp_source_dir}/${backward_cpp_file}")
      message(FATAL_ERROR
        "Vendored backward-cpp is incomplete: missing ${backward_cpp_file}")
    endif()
  endforeach()
  add_subdirectory(vendor/backward-cpp)
```

- [ ] **步骤 5：把完整上游树加入主仓库并验证内容没有漂移**

运行：

```powershell
git add -f -- .gitmodules 3rdparty/vendor/backward-cpp
git update-index --chmod=+x 3rdparty/vendor/backward-cpp/builds.sh

$moduleGitDir = git rev-parse --git-path modules/3rdparty/vendor/backward-cpp
$expected = @(& git --git-dir=$moduleGitDir ls-tree -r `
  3bb9240cb15459768adb3e7d963a20e1523a6294 | ForEach-Object {
    if ($_ -notmatch '^(?<mode>\d+) blob (?<sha>[0-9a-f]+)\t(?<path>.+)$') {
      throw "Unexpected upstream tree row: $_"
    }
    "$($Matches.mode) $($Matches.sha)`t$($Matches.path)"
  })
$actual = @(git ls-files -s -- 3rdparty/vendor/backward-cpp | ForEach-Object {
    if ($_ -match "`t3rdparty/vendor/backward-cpp/UPSTREAM.md$") { return }
    if ($_ -notmatch '^(?<mode>\d+) (?<sha>[0-9a-f]+) 0\t3rdparty/vendor/backward-cpp/(?<path>.+)$') {
      throw "Unexpected vendored tree row: $_"
    }
    "$($Matches.mode) $($Matches.sha)`t$($Matches.path)"
  })
$treeDifference = Compare-Object $expected $actual
if ($treeDifference) { $treeDifference; throw 'Vendored snapshot differs from upstream v1.6' }
if ($actual.Count -ne 24) { throw "Expected 24 upstream files, found $($actual.Count)" }
```

预期：24 个上游文件的 mode 和 blob SHA 完全一致；唯一新增文件是 `UPSTREAM.md`。

- [ ] **步骤 6：更新获取源码文档**

在 `README.md` 的克隆示例后加入：

```markdown
ZzPureTools remains a pinned submodule. backward-cpp v1.6 is vendored directly
in this repository and does not require separate submodule initialization or a
configure-time download.
```

在 `docs/BUILD.md` 的子模块初始化命令后加入：

```markdown
ZzPureTools is the only remaining Git submodule. backward-cpp v1.6 is tracked
directly under `3rdparty/vendor/backward-cpp`, so that dependency is available
offline after the ZzLogg source tree itself has been cloned or archived. This
does not make Qt, Boost, OpenSSL, or every CPM/CI dependency offline.
```

- [ ] **步骤 7：运行依赖布局契约并确认转换通过**

运行任务 1 步骤 2 的直接 CMake 脚本命令。

预期：退出码 0；`.gitmodules`、普通文件索引、来源记录和本地 `add_subdirectory` 全部通过。

- [ ] **步骤 8：用 disconnected 配置实际构建 backward-cpp**

运行：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --fresh -S . -B out/backward-offline-vs `
  -G 'Visual Studio 18 2026' -A x64 `
  -DKLOGG_BUILD_TESTS=ON `
  -DKLOGG_BUILD_UI2=OFF `
  -DKLOGG_BUILD_UI2_TESTS=OFF `
  -DKLOGG_USE_HYPERSCAN=OFF `
  -DKLOGG_USE_SENTRY=OFF `
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON `
  -DCPM_LOCAL_PACKAGES_ONLY=ON
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/backward-offline-vs `
  --config Debug --target backward_object
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/backward-offline-vs `
  -C Debug -R '^zzlogg_ui2\.dependency_layout$' --output-on-failure
```

预期：配置、`backward_object` 编译和依赖布局测试均退出码 0；输出不包含 backward-cpp 下载或子模块初始化。

- [ ] **步骤 9：提交 backward-cpp 离线化**

运行：

```powershell
git diff --check
git status --short
git add -f -- .gitmodules README.md docs/BUILD.md `
  cmake/VerifyDependencyLayout.cmake 3rdparty/CMakeLists.txt `
  3rdparty/vendor/backward-cpp
git update-index --chmod=+x 3rdparty/vendor/backward-cpp/builds.sh
git diff --cached --check
git commit -m "build: 将 backward-cpp 转为离线依赖"
```

预期：提交包含 `.gitmodules` 删除项、24 个原样上游文件、`UPSTREAM.md`、契约和文档；不包含 `.git` 元数据、`out` 或构建产物。

### 任务 3：双配置回归与线性集成准备

**文件：**
- 验证：`CMakePresets.json`
- 验证：`out/ui2-vs`
- 验证：Git 索引、子模块和历史

- [ ] **步骤 1：fresh 配置，并验证默认构建不产生 grep**

先使用以下脚本验证目标位于当前 worktree 的 `out` 目录，再删除旧构建副产物：

```powershell
$repo = [IO.Path]::GetFullPath((Resolve-Path '.').Path)
$outRoot = [IO.Path]::GetFullPath((Join-Path $repo 'out'))
$grepPaths = @(
  [IO.Path]::GetFullPath((Join-Path $repo 'out\ui2-vs\output\Debug\ZzLogg_grep.exe')),
  [IO.Path]::GetFullPath((Join-Path $repo 'out\ui2-vs\output\RelWithDebInfo\ZzLogg_grep.exe'))
)
foreach ($grepPath in $grepPaths) {
  if (-not $grepPath.StartsWith(
      $outRoot + [IO.Path]::DirectorySeparatorChar,
      [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove path outside out: $grepPath"
  }
  [IO.File]::Delete($grepPath)
}
```

然后运行：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --fresh --preset windows-vs2026-ui2 `
  -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
Test-Path out/ui2-vs/output/Debug/ZzLogg_grep.exe
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-relwithdebinfo
Test-Path out/ui2-vs/output/RelWithDebInfo/ZzLogg_grep.exe
```

预期：配置和两个构建退出码 0；两个 `Test-Path` 都为 `False`。

- [ ] **步骤 2：串行运行 Debug 与 RelWithDebInfo 完整 CTest**

为 Debug 设置隔离配置并运行：

```powershell
$repo = (Resolve-Path '.').Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:APPDATA = Join-Path $repo 'out\verification-config\debug\AppData'
$env:XDG_CONFIG_HOME = Join-Path $repo 'out\verification-config\debug\xdg'
New-Item -ItemType Directory -Force -Path $env:APPDATA,$env:XDG_CONFIG_HOME | Out-Null
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui2-vs `
  -C Debug --output-on-failure
```

Debug 完成后改用另一组目录运行 RelWithDebInfo：

```powershell
$repo = (Resolve-Path '.').Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:APPDATA = Join-Path $repo 'out\verification-config\rel\AppData'
$env:XDG_CONFIG_HOME = Join-Path $repo 'out\verification-config\rel\xdg'
New-Item -ItemType Directory -Force -Path $env:APPDATA,$env:XDG_CONFIG_HOME | Out-Null
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui2-vs `
  -C RelWithDebInfo --output-on-failure
```

预期：两套均为 21/21；不要并行执行，因为 icon generator 契约共享测试目录。

- [ ] **步骤 3：重建 portable 并复核安装器契约**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build `
  --preset windows-vs2026-ui2-relwithdebinfo --target klogg_portable_folder
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui2-vs `
  -C RelWithDebInfo -R '^zzlogg_ui2\.windows_installer_contract$' `
  --output-on-failure
```

预期：portable 构建和安装器契约均退出码 0；现有品牌、图标和完整 runtime staging 不回归。

- [ ] **步骤 4：执行最终 Git 完整性检查**

运行：

```powershell
git diff --check master...HEAD
git status --short
git submodule status
git ls-files -s -- 3rdparty/vendor/backward-cpp
git rev-list --merges master..HEAD
git log --oneline --decorate master..HEAD
```

预期：工作树干净；`git submodule status` 只列出 ZzPureTools 的 `f9e6c6f`；
backward-cpp 只包含普通文件 mode，不含 `160000`；历史没有 merge commit。

- [ ] **步骤 5：请求最终代码审查并按收尾技能交接**

使用 `requesting-code-review` 审查规格提交之后的全部依赖变更，重点检查：

- ZzPureTools 固定提交和构建兼容性；
- backward-cpp 24 个上游文件的完整性、许可证和非 gitlink 状态；
- 配置/构建期间不存在 backward-cpp 网络回退；
- 文档没有把“backward-cpp 离线”夸大成“项目完全离线”；
- 历史保持线性。

审查无 Critical/Important 后，使用 `verification-before-completion` 重跑步骤 2 和 Git
检查，再用 `finishing-a-development-branch` 给出本地线性合并、推送 PR 或保留分支三个
选项。未经用户选择不合并、不推送。
