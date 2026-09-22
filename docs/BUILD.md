# ZzLogg 构建指南

[返回项目首页](../README.md) · [用户手册](DOCUMENTATION.md) · [UI 代码学习指南](UI_CODE_GUIDE.md)

## 目录

- [获取源码](#获取源码)
- [构建要求](#构建要求)
- [使用预设构建](#使用预设构建)
- [直接使用命令行构建](#直接使用命令行构建)
- [框架链接方式](#框架链接方式)
- [数据目录与迁移](#数据目录与迁移)
- [安装与 CPack](#安装与-cpack)
- [Windows 独立运行目录](#windows-独立运行目录)
- [构建选项与目标](#构建选项与目标)
- [验证范围](#验证范围)

## 获取源码

当前仓库为 [github.com/jackfahdin/ZzLogg](https://github.com/jackfahdin/ZzLogg)。克隆时一并获取固定版本的子模块：

```bash
git clone --recursive https://github.com/jackfahdin/ZzLogg
cd ZzLogg
```

如果克隆时未使用 `--recursive`，请在配置前初始化子模块：

```bash
git submodule update --init --recursive
```

ZzPureTools 是固定版本的必需构建依赖，也是仓库唯一的 Git 子模块。Qt、Boost、OpenSSL，以及 CPM/CI 使用的依赖仍可能需要单独安装或联网获取。

## 构建要求

- 直接使用命令行构建需要 CMake 3.23 或更高版本；仓库内的预设和工作流需要 CMake 3.25 或更高版本。
- 编译器需支持 C++20：GCC 13.1+、Clang 17+、Apple Clang 15+，或 MSVC 19.38+（Visual Studio 2022 17.8+）。Apple 平台的 macOS 部署目标至少为 13.3。
- Qt 6.8 或更高版本，包含 Core、Core5Compat、Gui、Widgets、Svg、Concurrent、Network、Xml、LinguistTools 及匹配的私有开发文件。
  - `Core5Compat` 是 Qt 6 的模块，当前为日志编码检测和解码提供 `QTextCodec` / `QTextDecoder`，保留 GBK、Big5、Shift-JIS 等编码支持；不代表支持 Qt 5。移除此依赖需要先迁移编码层，并验证流式解码和已有编码设置。
  - CI 固定使用 Qt 6.11.2；本地源码构建的最低版本仍为 Qt 6.8。Linux CI 与 DEB 安装验证使用 Ubuntu 24.04，官方 Linux 包以该版本为基线。
- 固定版本的 ZzPureTools 子模块，以及仓库内随附的其他第三方依赖。
- Perl，用于生成高亮语法资源。首次配置会下载并校验 KSyntaxHighlighting 6.22.0 和 Extra CMake Modules 6.22.0；离线配置方式见[代码文本高亮](code-syntax-highlighting.md)。

UI 专项测试还需要 Qt Test。UI 测试预设将 macOS 部署目标设为 13.3；其他 macOS 配置也必须满足这一最低要求，可显式传入 `-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3`。

Hyperscan 搜索需要 SSSE3 指令集、Boost 头文件和 Ragel。依赖不可用时，传入 `-DKLOGG_USE_HYPERSCAN=OFF`，改用 Qt 正则表达式后端。其余第三方依赖由仓库提供或在 CMake 配置过程中解析。

选择 Ninja 预设时需要安装 Ninja。Windows 的 `windows-vs2026*` 预设使用 `Visual Studio 18 2026` 生成器，因此还需安装 Visual Studio 2026，以及支持该生成器的 CMake；仅满足通用最低版本不足以使用这些预设。使用其他受支持编译器时，可自行选择匹配的生成器。

## 使用预设构建

查看全部配置、构建、测试和工作流预设：

```bash
cmake --list-presets=all
```

### Ninja 工作流

共享 Ninja 工作流通过一条命令完成配置、构建和测试：

```bash
cmake --workflow --preset ninja-debug
cmake --workflow --preset ninja-relwithdebinfo
cmake --workflow --preset ninja-release
```

也可以分步执行，并在配置时添加参数：

```bash
cmake --preset ninja-release -DKLOGG_USE_HYPERSCAN=OFF
cmake --build --preset ninja-release
ctest --preset ninja-release
```

UI 专项测试使用相同的 ZzLogg 图形界面目标：

```bash
cmake --workflow --preset ninja-ui-debug
```

普通预设默认关闭 `KLOGG_BUILD_TESTS`；CTest 仅运行当前配置注册的测试。UI 预设通过 `KLOGG_BUILD_UI_TESTS=ON` 启用 UI 专项测试。

### Windows 本地预设

复制 `CMakeUserPresets.json.example` 为 `CMakeUserPresets.json`，设置本机 Qt 和 Visual Studio 路径。示例文件提供 `windows-qt6` 配置，以及对应的构建、测试和运行目录预设：

```powershell
cmake --preset windows-qt6
cmake --build --preset windows-qt6-relwithdebinfo
ctest --preset windows-qt6-relwithdebinfo
```

`CMakeUserPresets.json` 已被 Git 忽略，机器专用路径不会进入版本控制。

也可以直接使用仓库的 Visual Studio 2026 UI 预设，并显式提供 Qt 路径。以下路径仅为示例，请替换为本机安装位置：

```powershell
cmake --preset windows-vs2026-ui -DCMAKE_PREFIX_PATH=D:/SoftWare/Qt/6.11.0/msvc2022_64 -DKLOGG_USE_HYPERSCAN=OFF
cmake --build --preset windows-vs2026-ui-relwithdebinfo
ctest --preset windows-vs2026-ui-relwithdebinfo
```

## 直接使用命令行构建

不使用预设时，可配置常规构建目录。以下为 Bash 命令；在 PowerShell 中请将多行配置命令合并为一行，或使用 PowerShell 的续行语法：

```bash
cmake -S . -B out/build/ZzLogg -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKLOGG_USE_HYPERSCAN=OFF
cmake --build out/build/ZzLogg
ctest --test-dir out/build/ZzLogg --output-on-failure
```

唯一的图形界面程序为 `ZzLogg`（Windows 下为 `ZzLogg.exe`）。实验性的 `klogg_grep` 目标不参与默认构建；开发该命令行前端时可显式构建：

```bash
cmake --build out/build/ZzLogg --target klogg_grep
```

默认构建不会生成其他图形界面程序或额外的独立可执行文件目标。

## 框架链接方式

ZzLogg 将 ZzPureTools 及其框架依赖构建为静态库。该设置由本仓库的 `cmake/ZzPureTools.cmake` 控制，不修改固定版本的子模块。Qt 和编译器运行库保持动态链接。

部署后的 Windows 运行目录不应包含 ZzPureTools、ZzLog 或 QWK 的 DLL。切换链接方式后需要重新生成运行目录；旧构建输出目录可能仍留有之前动态库构建产生的 DLL。

Linux GNU 运行库的打包由宿主应用单独处理，与框架采用静态链接无关。

## 数据目录与迁移

首次启动时，ZzLogg 会询问配置、已保存会话和日志的存储位置。取消选择会退出，不创建配置。可选位置包括：

- **用户数据目录**：平台为当前用户提供的应用数据目录。
- **程序目录**：程序旁的 `data/` 目录，适合让应用与数据放在一起的绿色使用方式。
- **自定义目录**：用户选择的绝对路径。

所选根目录包含 `config/ZzLogg.ini`、`session/ZzLogg_session.ini`、`logs/` 和 `storage-manifest.ini`。`--data-dir <absolute-path>` 只覆盖当前进程的数据位置，不替换已保存的存储定位信息。

“首选项”中的“存储”页可将现有数据迁移到新的空目录。ZzLogg 会保存当前设置，以事务方式执行迁移，并询问立即重启还是稍后重启。新位置在重启后生效，已保存会话和最近文件会保留。若所选根目录之后不可用，启动时会报告存储错误，不会静默创建新的默认配置。

界面显示的厂商名称为 `Jackfahdin`。Qt 存储命名空间和应用标识保持兼容，以保留已有数据位置。

## 安装与 CPack

将已配置的 Ninja 构建安装到暂存目录：

```bash
cmake --install out/build/ninja-release --prefix staging/ZzLogg
```

Linux 安装规则会将 `ZzLogg.desktop`、16、32、48、64、128、256、512 像素的 PNG 图标，以及可缩放的 `ZzLogg.svg` 安装到标准位置。受支持的 Unix 配置会生成 CPack 配置，并使用统一的产品元数据。

Windows 安装脚本为 `packaging/windows/ZzLogg.iss`，使用 Inno Setup 7.1.0 内置的 Windows 11 风格向导，支持跟随系统明暗主题，以及英文、简体中文和繁体中文。CI 通过 `packaging/windows/Build-InnoInstaller.ps1` 下载官方 x64 编译器并校验固定 SHA-256，不依赖 runner 预装版本。

在 Windows 上准备完整 `release/` 运行目录、`txpayload/ZzLoggUpdateTx.exe` 后执行：

```powershell
./packaging/windows/GenerateInstallerManifest.ps1 -StagingDirectory release
./packaging/windows/Build-InnoInstaller.ps1 -Version 26.09.05 -Platform x64
```

产物仍为 `ZzLogg-26.09.05-x64-Qt6-setup.exe`。无人值守安装使用 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`，自定义目录使用 `/DIR="C:\Apps\ZzLogg"`。安装器默认采用已登记的安装目录，可直接覆盖旧版 NSIS 安装；不会执行会清理用户配置的旧卸载器。卸载仅删除安装器所属文件，保留额外用户文件和 AppData 配置。跨安装器覆盖与受限升级入口需在 Windows 上执行验收。

源码中维护 macOS 应用包、发行元数据和 DMG 布局；实际构建与检查必须在 macOS 主机上完成。

## Windows 独立运行目录

### 生成与启动

配置 Windows 构建并确保 `windeployqt` 可用后，生成独立运行目录：

```powershell
cmake --build --preset windows-vs2026-ui-relwithdebinfo --target zzlogg_runtime_folder
```

生成位置为 `<build-directory>/runtime/RelWithDebInfo/ZzLogg-runtime/`。其中包含唯一的图形界面程序 `ZzLogg.exe`，以及所需 Qt、MSVC 运行库和 TBB 动态依赖；ZzPureTools 及其框架依赖已经静态链接进程序。

Windows 打包会将同一目录树复制到安装程序和独立归档包的暂存目录，两种形式均运行同一个 `ZzLogg.exe`。请从完整运行目录启动程序。

### 部署内容与精简规则

运行目录面向使用栅格绘制的 QWidget 应用，部署时排除软件 OpenGL、D3D/DXC 编译器、PDF、不常用图像格式、通用 TUIO 插件和外部样式插件。保留 Qt 内置的 PNG、JPEG、ICO、SVG、Windows 平台插件和原生 HTTPS 支持。

帮助以简体中文、繁体中文和英文内嵌于可执行文件，随界面语言切换；运行目录不分发源码 README 或独立帮助文件。现有许可证与声明集中保存在 `licenses/` 下，运行目录根部不再放置重复副本。重新构建 `zzlogg_runtime_folder` 即可应用部署规则。

## 构建选项与目标

| 选项或目标 | 用途 |
| --- | --- |
| `KLOGG_BUILD_UI_TESTS=ON` | 启用 UI 专项测试 |
| `KLOGG_USE_HYPERSCAN=OFF` | 使用 Qt 正则表达式后端 |
| `zzlogg_runtime_folder` | 生成 Windows 独立运行目录 |

这些选项不会创建第二个图形界面程序。公开的可执行文件、安装包和桌面入口仍统一使用 ZzLogg 名称。

## 验证范围

### Windows Release UI 验收

在已配置的开发环境中执行以下命令，或像示例一样显式提供 Qt 路径：

```powershell
cmake --preset windows-vs2026-ui -DCMAKE_PREFIX_PATH=D:/SoftWare/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-vs2026-ui-release --parallel 8
ctest --preset windows-vs2026-ui-release
cmake --build --preset windows-vs2026-ui-release --target zzlogg_runtime_folder
```

对应的 `windows-vs2026-ui-release` 工作流依次执行配置、构建和测试。使用工作流时，请通过环境或匹配的本地用户预设提供 Qt 路径。缺少 Hyperscan 依赖时，同样需要关闭 `KLOGG_USE_HYPERSCAN`。

Release 测试预设运行所有已注册的测试，包括 `klogg_smoke`。运行目录为 `out/ui-vs/runtime/Release/ZzLogg-runtime/`，应从该目录启动 `ZzLogg.exe`，而不是从缺少依赖的构建输出目录启动；验收无需先制作 ZIP。

### UI 测试入口

应用 UI 实现在 `src/ui`。UI 验收测试位于 `tests/ui_acceptance`，CTest 名称统一为 `zzlogg_ui.*`；已有核心集成测试位于 `tests/ui`。

使用 `KLOGG_BUILD_UI_TESTS` 和 `*-ui-*` 预设运行 UI 测试。Ninja UI 和 Windows UI 的 Debug、RelWithDebInfo 测试预设只选择 `zzlogg_ui.*`，Windows UI Release 预设运行全部已注册测试。

早期 UI2 选项和预设别名已移除，本地脚本应使用上述入口。应用使用同一个 Qt Widgets 图形界面目标。

### 平台验证边界

CTest 预设只覆盖当前主机上可运行的测试。Windows 交互式 DPI、主题、高对比度和多显示器检查，以及 Linux、macOS 的真实主机打包检查，都应在对应平台完成后再宣称发行验证通过。
