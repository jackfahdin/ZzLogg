# Extra CMake Modules 本地依赖

- 版本：6.31.0，标签 `v6.31.0`；引入日期：2026-11-13。
- 仓库：https://invent.kde.org/frameworks/extra-cmake-modules
- 标签对应提交：`2958379fb49c71129f119ec17f88df0b5ea21550`。
- 官方发布源码包：https://download.kde.org/stable/frameworks/6.31/extra-cmake-modules-6.31.0.tar.xz
- 官方 SHA-256：https://download.kde.org/stable/frameworks/6.31/extra-cmake-modules-6.31.0.tar.xz.sha256
- 经核对的 SHA-256：`8f74af329e4c918358bfe54316f8cf4d530c28444595a3e2fb15075f72877f17`。
- 导入方式：完整解压官方源码包并作为普通文件纳入 Git，不使用子模块。
- 本地修改：上游文件不修改，仅新增本说明；宿主构建适配位于 `cmake/ZzSyntaxHighlighting.cmake`。
- 构建要求：CMake 3.30+；KSyntaxHighlighting 另需 Qt 6.10+ 和 Perl。
- 许可证：主要模块采用 BSD-3-Clause；逐文件声明及 LICENSES/ 保留在源码中。

KSyntaxHighlighting 静态链接并内嵌语法和主题资源；ECM 只在构建时使用。
源码固定在本目录，配置时不下载、不读取旧的 `FETCHCONTENT_SOURCE_DIR_ZZLOGG_*` 覆盖路径。
更新版本时同时同步宿主版本要求、依赖总表及 `3rdparty/licenses/KSyntaxHighlighting/` 的发布声明。
