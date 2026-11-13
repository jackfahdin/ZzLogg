# KSyntaxHighlighting 本地依赖

- 版本：6.31.0，标签 `v6.31.0`；引入日期：2026-11-13。
- 仓库：https://invent.kde.org/frameworks/syntax-highlighting
- 标签对应提交：`6d8ab7945958e7354295db7f399cce06f7de7571`。
- 官方发布源码包：https://download.kde.org/stable/frameworks/6.31/syntax-highlighting-6.31.0.tar.xz
- 官方 SHA-256：https://download.kde.org/stable/frameworks/6.31/syntax-highlighting-6.31.0.tar.xz.sha256
- 经核对的 SHA-256：`7c2a64d3fd42ae433e9737b9cf6733b9ad3cb0e212285fd6820157d86831316c`。
- 导入方式：完整解压官方源码包并作为普通文件纳入 Git，不使用子模块。
- 本地修改：上游文件不修改，仅新增本说明；宿主构建适配位于 `cmake/ZzSyntaxHighlighting.cmake`。
- 构建要求：CMake 3.30+；KSyntaxHighlighting 另需 Qt 6.10+ 和 Perl。
- 许可证：库采用 MIT；语法、主题及其他文件遵循各自声明，许可证文本见 LICENSES/。

KSyntaxHighlighting 静态链接并内嵌语法和主题资源；ECM 只在构建时使用。
源码固定在本目录，配置时不下载、不读取旧的 `FETCHCONTENT_SOURCE_DIR_ZZLOGG_*` 覆盖路径。
更新版本时同时同步宿主版本要求、依赖总表及 `3rdparty/licenses/KSyntaxHighlighting/` 的发布声明。
