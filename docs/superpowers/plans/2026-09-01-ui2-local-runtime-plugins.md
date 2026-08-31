# UI2 本地运行时插件实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让 Windows 普通构建目录中的 `ZzLogg_ui2.exe` 不依赖本机 Qt 插件路径即可双击启动。

**架构：** 扩展现有构建后部署辅助函数，把 Qt SVG 图标引擎和 SVG 图像格式插件复制到标准插件子目录。新增一个 Windows CTest，验证真实文件布局，并在清除 `QT_PLUGIN_PATH` 后运行真实应用冒烟模式。

**技术栈：** CMake 3.23+、Qt 6、CTest、MSVC 多配置生成器。

---

## 文件结构

- `tests/ui2/localruntimecontracttest.cmake`：验证普通输出目录的插件闭包并启动真实应用。
- `tests/ui2/CMakeLists.txt`：注册仅 Windows 运行的本地运行时合约测试。
- `cmake/ZzPureTools.cmake`：为 UI2 目标复制 Qt SVG 插件。

### 任务 1：建立会失败的本地运行时合约

**文件：**
- 创建：`tests/ui2/localruntimecontracttest.cmake`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写失败的测试**

在测试脚本中根据 `CONFIG` 计算 Qt 插件后缀，逐一检查：

```cmake
set(expected_plugins
  "platforms/qwindows${qt_debug_suffix}.dll"
  "iconengines/qsvgicon${qt_debug_suffix}.dll"
  "imageformats/qsvg${qt_debug_suffix}.dll")
```

随后清除 `QT_PLUGIN_PATH`，以隔离的 `APPDATA`/`LOCALAPPDATA` 和 `ZZLOGG_UI2_SMOKE_MS=800` 启动传入的真实 `APP`。

- [ ] **步骤 2：重新配置并验证红灯**

运行：

```powershell
cmake --fresh --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
ctest --test-dir out/ui2-vs -C RelWithDebInfo -R zzlogg_ui2.windows_local_runtime_contract --output-on-failure
```

预期：FAIL，明确报告 `iconengines/qsvgicon.dll` 缺失。

### 任务 2：补齐普通构建的 Qt SVG 插件

**文件：**
- 修改：`cmake/ZzPureTools.cmake`
- 测试：`tests/ui2/localruntimecontracttest.cmake`

- [ ] **步骤 1：编写最小实现**

在 `klogg_copy_ui2_runtime_dlls()` 的 Windows 构建后命令中创建 `iconengines` 和 `imageformats`，并复制：

```cmake
$<TARGET_FILE:Qt6::QSvgIconPlugin>
$<TARGET_FILE:Qt6::QSvgPlugin>
```

- [ ] **步骤 2：构建 UI2 并验证绿灯**

运行：

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_ui2
ctest --test-dir out/ui2-vs -C RelWithDebInfo -R zzlogg_ui2.windows_local_runtime_contract --output-on-failure
```

预期：测试通过，真实应用在没有 `QT_PLUGIN_PATH` 的情况下完成冒烟运行。

### 任务 3：双配置回归、提交与线性集成

**文件：**
- 验证：`cmake/ZzPureTools.cmake`
- 验证：`tests/ui2/CMakeLists.txt`
- 验证：`tests/ui2/localruntimecontracttest.cmake`

- [ ] **步骤 1：构建并测试 Debug**

```powershell
cmake --build --preset windows-vs2026-ui2-debug
ctest --test-dir out/ui2-vs -C Debug --output-on-failure
```

预期：完整构建退出码 0，全部测试通过，输出目录包含带 `d` 后缀的 SVG 插件。

- [ ] **步骤 2：构建并测试 RelWithDebInfo**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure
```

预期：完整构建退出码 0，全部测试通过，输出目录包含无 `d` 后缀的 SVG 插件。

- [ ] **步骤 3：提交实现**

```powershell
git add cmake/ZzPureTools.cmake tests/ui2/CMakeLists.txt tests/ui2/localruntimecontracttest.cmake docs/superpowers
git commit -m "fix: 补齐 UI2 本地 Qt 插件"
```

- [ ] **步骤 4：纯快进合并到主线并复验**

在主工作区运行：

```powershell
git merge --ff-only codex/fix-ui2-runtime-plugins
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_ui2
ctest --test-dir out/ui2-vs -C RelWithDebInfo -R zzlogg_ui2.windows_local_runtime_contract --output-on-failure
```

预期：`master` 纯快进，无 Merge 提交，主工作区真实应用通过本地运行时测试。
