# ZzPureTools 更新与 backward-cpp 离线化设计

日期：2026-08-31
状态：已批准，待书面规格复核

## 目标

本次变更完成两件事：

1. 将 `3rdparty/vendor/ZzPureTools` 子模块更新并固定到远端 `master` 当前提交
   `f9e6c6f25af8061666cc04da2a43c0a1d3cfe271`。
2. 将 `3rdparty/vendor/backward-cpp` 从 Git 子模块转换为仓库直接跟踪的普通
   vendored 源码，使 backward-cpp 不再需要网络或 `git submodule update` 才能参与配置和
   构建。

本设计只保证 backward-cpp 自身离线可用，不宣称 ZzLogg 的其他 CPM、Boost、Qt、OpenSSL
或 CI 依赖已经全部离线化。

## 依赖所有权与版本策略

### ZzPureTools

ZzPureTools 继续作为独立 Git 子模块维护。主仓库只更新 gitlink，不复制其源码历史，也不把
它转换为 vendored 普通目录。更新目标固定为提交
`f9e6c6f25af8061666cc04da2a43c0a1d3cfe271`，避免构建随远端分支继续变化。

更新后若 ZzPureTools 的 CMake target、Qt 组件或公开接口发生不兼容，只在 ZzLogg 中完成
本次构建所必需的最小适配，不顺带重构 UI 或扩大功能范围。

### backward-cpp

backward-cpp 固定使用现有 v1.6 源码，对应上游提交
`3bb9240cb15459768adb3e7d963a20e1523a6294`。转换时：

- 从 `.gitmodules` 删除 `3rdparty/vendor/backward-cpp` 条目；
- 删除该路径的 gitlink；
- 将同一提交的完整工作树作为普通文件加入主仓库；
- 不导入 backward-cpp 的 Git 历史；
- 保留上游 LICENSE、README、CMake 文件以及构建所需源码；
- 不提交其 `.git` 元数据、构建目录或生成物；
- 增加简短的上游来源记录，明确项目、版本、提交和许可证来源。

主项目继续通过现有的 `add_subdirectory(vendor/backward-cpp)` 使用该依赖，不引入
`FetchContent`、CPM 下载回退或配置期解压流程。

## 仓库与构建行为

转换完成后，`.gitmodules` 只保留 ZzPureTools。普通克隆若只需要 backward-cpp，不再需要
初始化该依赖的子模块。ZzPureTools 仍按现有文档执行子模块初始化。

CMake 在启用使用 backward-cpp 的目标时必须只读取
`3rdparty/vendor/backward-cpp` 的已跟踪本地文件。该目录缺失或不完整时，配置阶段应给出
明确错误，不允许静默联网补齐。

现有 UI2 默认构建、`klogg`、`ZzLogg_portable`、显式测试目标和打包行为保持不变。新版
ZzPureTools 不得让默认构建重新产生 `ZzLogg_grep`。

## 测试与验收

实现采用测试先行，至少覆盖以下契约：

1. `.gitmodules` 不再声明 backward-cpp，只声明 ZzPureTools。
2. Git 索引中的 `3rdparty/vendor/backward-cpp` 是普通文件树，不是 mode `160000` 的
   gitlink。
3. backward-cpp 的版本/提交来源记录和许可证文件存在。
4. CMake 仍从本地路径添加 backward-cpp，源码缺失时明确失败，且不存在针对该依赖的
   `FetchContent`、CPM 或下载回退。
5. ZzPureTools gitlink 精确固定到
   `f9e6c6f25af8061666cc04da2a43c0a1d3cfe271`。
6. Windows Debug 与 RelWithDebInfo 配置、构建及完整 CTest 通过。
7. 默认构建仍不生成 `ZzLogg_grep.exe`；portable 与既有品牌、图标及安装器契约继续通过。
8. Git 历史保持线性，不创建 merge commit，不自动推送。

为了验证 backward-cpp 的离线边界，测试不得依赖已经初始化的 backward-cpp 子模块元数据；
它应直接使用主仓库跟踪的源码。其他第三方依赖仍可使用既有缓存和接入方式，测试结果不会被
描述成“整个项目完全离线”。

## 提交与回退边界

设计文档单独提交。实现阶段至少分为两个可审查提交：

1. 更新 ZzPureTools gitlink，并完成必要兼容适配；
2. 将 backward-cpp 转为普通 vendored 源码，并加入离线契约与文档。

若 ZzPureTools 新提交无法在当前 Qt 6/MSVC 环境通过最小适配验证，停止在实现分支并报告
具体不兼容，不回退或重写用户已有提交。若 backward-cpp 转换验证失败，保留转换前提交作为
可恢复边界，不使用破坏性 Git 操作。
