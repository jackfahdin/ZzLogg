# 本地依赖包目录

本目录（`3rdparty/vendor/`）用于存放项目在受限网络环境下构建所需的第三方依赖本地副本。

## 本地化的依赖

| 目录 | 名称 | 版本 | 作用 | 详细说明 |
|------|------|------|------|----------|
| `simdutf/` | simdutf | 5.6.2 | 高性能 UTF 编码转换与校验 | [simdutf/README.md](simdutf/README.md) |
| `type_safe/` | type_safe | 0.2.4 | 强类型安全包装库 | [type_safe/README_Z.md](type_safe/README_Z.md) |
| `CRoaring/` | CRoaring | 4.2.1 | 压缩位图索引 | [CRoaring/README_Z.md](CRoaring/README_Z.md) |
| `streamvbyte/` | streamvbyte | 1.0.0 | SIMD 整数压缩 | [streamvbyte/README_Z.md](streamvbyte/README_Z.md) |
| `macdeployqtfix/` | macdeployqtfix | df88850 | macOS Qt 打包 rpath 修复脚本 | [macdeployqtfix/README_Z.md](macdeployqtfix/README_Z.md) |
| `maddy/` | maddy | 602e266 | Markdown 转 HTML 工具 | [maddy/README.md](maddy/README.md) |
| `hyperscan/` | hyperscan | 0931a40 | 高性能正则表达式匹配引擎 | [hyperscan/README_Z.md](hyperscan/README_Z.md) |
| `vectorscan/` | vectorscan | d29730e | Hyperscan 多架构移植版 | [vectorscan/README_Z.md](vectorscan/README_Z.md) |
| `uchardet/` | uchardet | 0.0.8 | 字符编码自动检测 | [uchardet/README_Z.md](uchardet/README_Z.md) |
| `klogg_karchive/` | klogg_karchive | f546bf6 | KF5Archive Qt5/Qt6 兼容 fork | [klogg_karchive/README_Z.md](klogg_karchive/README_Z.md) |
| `robin-hood-hashing/` | robin_hood | 3.11.5 | 快速哈希表/集合 | [robin-hood-hashing/README_Z.md](robin-hood-hashing/README_Z.md) |
| `backward-cpp/` | backward-cpp | 3bb9240 | C++ 崩溃堆栈跟踪 | [backward-cpp/README_Z.md](backward-cpp/README_Z.md) |
| `Catch2/` | Catch2 | 2.13.8 | C++ 测试框架 | [Catch2/README_Z.md](Catch2/README_Z.md) |
| `KDSingleApplication/` | KDSingleApplication | 5b30db3 | 单实例应用支持 | [KDSingleApplication/README_Z.md](KDSingleApplication/README_Z.md) |
| `xxHash/` | xxHash | 0.8.1 | 高速哈希算法 | [xxHash/README_Z.md](xxHash/README_Z.md) |
| `whereami/` | whereami | dcb52a0 | 可执行文件路径获取 | [whereami/README_Z.md](whereami/README_Z.md) |
| `klogg_exprtk/` | exprtk | 1f9f4cd | 数学表达式解析 | [klogg_exprtk/README_Z.md](klogg_exprtk/README_Z.md) |
| `efsw/` | efsw | 1.4.1 | 文件系统监控 | [efsw/README_Z.md](efsw/README_Z.md) |
| `KDToolBox/` | KDToolBox | 6468867d | Qt 工具箱（klogg 仅使用 KDSignalThrottler） | [KDToolBox/README_Z.md](KDToolBox/README_Z.md) |
| `oneTBB/` | oneTBB | c9be1ac2 | Intel oneAPI Threading Building Blocks | [oneTBB/README_Z.md](oneTBB/README_Z.md) |
| `mimalloc/` | mimalloc | 2.1.7 | 微软高性能内存分配器 | [mimalloc/README_Z.md](mimalloc/README_Z.md) |
| `sentry-native/` | sentry-native | a3d58622 | Sentry 原生崩溃报告 SDK（可选） | [sentry-native/README_Z.md](sentry-native/README_Z.md) |

## 注意事项
- 更新依赖时，需同步更新对应子目录的 `README.md` 和本总览文件中的版本号或者commit hash。
