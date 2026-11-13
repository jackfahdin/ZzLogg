# 本地依赖包目录

本目录（`3rdparty/vendor/`）存放直接跟踪的第三方依赖副本，以及由 Git 子模块固定版本的 ZzPureTools。KDE 下载依赖的入口另列于下方；本目录存在不代表整个项目可离线配置。

## 本地化的依赖

| 目录 | 名称 | 版本 | 作用 | 详细说明 |
|------|------|------|------|----------|
| `simdutf/` | simdutf | 5.6.2 | 高性能 UTF 编码转换与校验 | [simdutf/README.md](simdutf/README.md) |
| `type_safe/` | type_safe | 0.2.4 | 强类型安全包装库 | [type_safe/README_Z.md](type_safe/README_Z.md) |
| `CRoaring/` | CRoaring | 4.2.1 | 压缩位图索引 | [CRoaring/README.md](CRoaring/README.md) |
| `streamvbyte/` | streamvbyte | 1.0.0 | SIMD 整数压缩 | [streamvbyte/README_Z.md](streamvbyte/README_Z.md) |
| `macdeployqtfix/` | macdeployqtfix | df88850 | macOS Qt 打包 rpath 修复脚本 | [macdeployqtfix/README_Z.md](macdeployqtfix/README_Z.md) |
| `maddy/` | maddy | 602e266 | Markdown 转 HTML 工具 | [maddy/README.md](maddy/README.md) |
| `hyperscan/` | hyperscan | 0931a40 | 高性能正则表达式匹配引擎 | [hyperscan/README_Z.md](hyperscan/README_Z.md) |
| `vectorscan/` | vectorscan | d29730e | Hyperscan 多架构移植版 | [vectorscan/README_Z.md](vectorscan/README_Z.md) |
| `uchardet/` | uchardet | 0.0.8 | 字符编码自动检测 | [uchardet/README_Z.md](uchardet/README_Z.md) |
| `klogg_karchive/` | klogg_karchive | f546bf6 | KF5Archive Qt5/Qt6 兼容 fork | [klogg_karchive/README_Z.md](klogg_karchive/README_Z.md) |
| `robin-hood-hashing/` | robin_hood | 3.11.5 | 快速哈希表/集合 | [robin-hood-hashing/README_Z.md](robin-hood-hashing/README_Z.md) |
| `Catch2/` | Catch2 | 2.13.8 | C++ 测试框架 | [Catch2/README_Z.md](Catch2/README_Z.md) |
| `KDSingleApplication/` | KDSingleApplication | 5b30db3 | 单实例应用支持 | [KDSingleApplication/README_Z.md](KDSingleApplication/README_Z.md) |
| `xxHash/` | xxHash | 0.8.1 | 高速哈希算法 | [xxHash/README_Z.md](xxHash/README_Z.md) |
| `klogg_exprtk/` | exprtk | 1f9f4cd | 数学表达式解析 | [klogg_exprtk/README_Z.md](klogg_exprtk/README_Z.md) |
| `efsw/` | efsw | 1.4.1 | 文件系统监控 | [efsw/README_Z.md](efsw/README_Z.md) |
| `KDToolBox/` | KDToolBox | 6468867d | Qt 工具箱（klogg 仅使用 KDSignalThrottler） | [KDToolBox/README_Z.md](KDToolBox/README_Z.md) |
| `oneTBB/` | oneTBB | c9be1ac2 | Intel oneAPI Threading Building Blocks | [oneTBB/README_Z.md](oneTBB/README_Z.md) |
| `mimalloc/` | mimalloc | 2.1.7 | 微软高性能内存分配器 | [mimalloc/README_Z.md](mimalloc/README_Z.md) |
| `monocypher/` | Monocypher | 4.0.3 | 无 Qt 更新核心的 Ed25519 签名验证 | [来源与校验摘要](monocypher/README.zzlogg.md) |
| `nlohmann-json/` | nlohmann/json | 3.12.0 | 更新协议的严格 JSON 解析 | [来源与校验摘要](nlohmann-json/README.zzlogg.md) |
| `ZzPureTools/` | ZzPureTools | d8afb260 | Qt UI 框架，按组件静态集成 | [框架说明](ZzPureTools/README.md) |

## 构建入口与启用条件

- 常规依赖见 [3rdparty/CMakeLists.txt](../CMakeLists.txt)；Catch2 仅在 `KLOGG_BUILD_TESTS=ON` 时构建，maddy 用于构建期手册生成，macdeployqtfix 仅用于 macOS 打包。
- Hyperscan 和 Vectorscan 由 `KLOGG_USE_HYPERSCAN`、`KLOGG_USE_VECTORSCAN` 控制，标准 preset 均关闭；实际选择遵循 CMake 的 `if/elseif` 分支。
- Monocypher 和 nlohmann/json 属于[独立更新核心](../../src/update/CMakeLists.txt)；其来源文件未修改，许可证分别采用 [BSD-2-Clause 选项](monocypher/LICENCE.md)和 [MIT](nlohmann-json/LICENSE.MIT)。
- ZzPureTools 来自 [jackfahdin/ZzPureTools](https://github.com/jackfahdin/ZzPureTools)，由 [Git 子模块声明](../../.gitmodules)及 gitlink 固定；初始化使用 `git submodule update --init --recursive`，包含嵌套的 ZzLog。宿主在 [cmake/ZzPureTools.cmake](../../cmake/ZzPureTools.cmake) 中配置组件并关闭其测试、示例和 Quick 构建。框架使用 [MIT](ZzPureTools/LICENSE)，内部依赖适用[各自的许可与补丁记录](ZzPureTools/docs/third-party/THIRD_PARTY_NOTICES.md)。宿主侧适配不修改子模块源码。

## 下载依赖

| 名称 | 固定版本 | 用途与来源 | 许可证及本地集成 |
| --- | --- | --- | --- |
| KSyntaxHighlighting | 6.22.0 | 代码语法高亮；[KDE 源码包](https://download.kde.org/stable/frameworks/6.22/syntax-highlighting-6.22.0.tar.xz) | 库使用 MIT，语法与主题资源各有许可；静态链接并内嵌资源 |
| Extra CMake Modules | 6.22.0 | KSyntaxHighlighting 构建模块；[KDE 源码包](https://download.kde.org/stable/frameworks/6.22/extra-cmake-modules-6.22.0.tar.xz) | BSD-3-Clause，仅构建期使用 |

两项均由 [cmake/ZzSyntaxHighlighting.cmake](../../cmake/ZzSyntaxHighlighting.cmake) 下载并校验 SHA-256，集成选项保留在宿主 CMake 中。摘要和许可证见 [KSyntaxHighlighting 记录](../licenses/KSyntaxHighlighting/README.txt)，发布时由 [StageRuntimeLicenses.cmake](../../cmake/StageRuntimeLicenses.cmake) 收集。

## 注意事项
- 更新依赖时，需同步更新对应子目录的 `README.md` 和本总览文件中的版本号或者commit hash。
- 更新 ZzPureTools gitlink 时，同时更新 [依赖布局检查](../../cmake/VerifyDependencyLayout.cmake) 中的固定提交号，并在暂存新 gitlink 后运行该检查。
