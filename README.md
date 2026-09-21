# ZzLogg

ZzLogg 是一款跨平台桌面日志查看工具，用于浏览、跟随、过滤和搜索大型或复杂日志文件。它直接从磁盘读取文件，将搜索结果与原始日志同时呈现，支持正则表达式、布尔搜索表达式和文件持续追加时的实时跟随。

当前源码仓库：[github.com/jackfahdin/ZzLogg](https://github.com/jackfahdin/ZzLogg)。

![ZzLogg 主窗口](website/static/screenshots/mainwindow.png)

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [用户手册](docs/DOCUMENTATION.md) | 文件操作、搜索、快捷键和设置；另有 [English](docs/i18n/en/DOCUMENTATION.md) 与 [繁體中文](docs/i18n/zh_TW/DOCUMENTATION.md) |
| [构建指南](docs/BUILD.md) | 依赖、平台要求、CMake 预设、测试、安装与运行目录 |
| [UI 代码学习指南](docs/UI_CODE_GUIDE.md) | 界面源码结构、职责划分与建议阅读顺序 |

应用内帮助内嵌简体中文、繁体中文和英文手册，并随界面语言切换。

## 主要功能

- 打开大型文本文件，无需将整个文件载入内存。
- 使用 Qt 正则表达式或可选的 Hyperscan 后端进行搜索。
- 通过 `and`、`or`、`not` 组合搜索表达式。
- 显示过滤结果、匹配上下文、标记和颜色高亮。
- 跟随持续增长的日志，并检测追加或覆盖写入。
- 打开本地文件、支持的压缩文件与归档、远程 URL 和剪贴板文本。
- 支持保存会话、最近文件、收藏、编码检测和多窗口。
- 提供 Windows、Linux 和 macOS 的 Qt 6 构建配置；各平台安装包需在对应主机上验证。

## 快速构建

克隆仓库并初始化子模块：

```bash
git clone --recursive https://github.com/jackfahdin/ZzLogg
cd ZzLogg
```

ZzPureTools 是固定版本的必需构建依赖，也是仓库唯一的 Git 子模块。

构建需要支持 C++20 的编译器、CMake 3.23 或更高版本、Perl，以及 Qt 6.8 或更高版本。使用仓库内的预设需要 CMake 3.25 或更高版本；典型的 Ninja 构建如下：

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release
ctest --preset ninja-release
```

未安装 Boost 和 Ragel 时，在配置命令中添加 `-DKLOGG_USE_HYPERSCAN=OFF`，使用 Qt 正则表达式后端。详细的平台要求、UI 专项测试预设、安装命令和 Windows 运行目录目标见[构建指南](docs/BUILD.md)。

默认构建只生成一个图形界面程序 `ZzLogg`（Windows 下为 `ZzLogg.exe`）。实验性的命令行目标 `klogg_grep` 需要显式构建，不属于默认构建内容。

## 数据存储与绿色使用

首次启动时，ZzLogg 会询问持久化数据的保存位置：用户数据目录、自定义绝对路径，或程序旁的 `data/` 目录。选择程序目录即可绿色使用，让程序、配置、会话和日志保存在一起。取消选择会退出程序，不创建配置。

所有存储模式使用相同的目录结构：`config/ZzLogg.ini`、`session/ZzLogg_session.ini`、`logs/` 和 `storage-manifest.ini`。

之后可在“首选项 → 存储”中选择新的空目录。ZzLogg 会迁移现有数据，并提供“立即重启”或“稍后重启”；新位置在重启后生效，已保存的会话和最近文件会保留。

## 打包与安装

仓库维护 CMake install/CPack 配置、Windows Qt 6 Inno Setup 7 安装脚本、Linux 桌面入口与图标安装规则，以及由 CMake 生成的 Windows 独立运行目录。制作安装包需要对应平台工具，并应在目标主机上验证。

ZzPureTools 及其框架依赖采用静态链接，Qt 和编译器运行库采用动态链接。Windows 运行目录包含应用运行所需的动态依赖，许可证集中保存在 `licenses/` 下。

预编译安装包可从 [GitHub Releases](https://github.com/jackfahdin/ZzLogg/releases) 下载：[最新稳定版](https://github.com/jackfahdin/ZzLogg/releases/latest)、[每日预览版](https://github.com/jackfahdin/ZzLogg/releases/tag/continuous-build)。也可以按照构建指南从源码构建。

## 参与贡献

请基于当前 GitHub 仓库提交变更。平台打包修改应与 `cmake/ZzLoggBrand.cmake` 中的统一产品元数据保持一致，并在提交前运行相关 CMake/CTest 预设。

## 来源与许可

ZzLogg 源自 [klogg](https://github.com/variar/klogg)，而 klogg 源自 [glogg](https://github.com/nickbnf/glogg)。本项目保留 Anton Filimonov、Nicolas Bonnefon 及其他贡献者的成果与署名。

ZzLogg 是自由软件，遵循 GNU 通用公共许可证第 3 版或更新版本（GPLv3+）。完整许可和附加声明见 [COPYING](COPYING) 与 [NOTICE](NOTICE)。
