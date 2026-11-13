# 第一批依赖清理实现计划

**目标：** 按[优化路线](2026-11-13-dependency-performance-roadmap.md)完成 P0 的构建基线、P1，以及 P2 中已核实的失效记录和工具清理。

**架构：** 删除没有调用者的 whereami 构建目标及源码；移除已停用的 Windows 签名客户端和对应空接口。保持现有应用行为、打包步骤和更新签名协议。

**技术栈：** CMake、C/C++、Qt 6、GitHub Actions。当前会话逐项执行和验证，每项独立提交。

## 范围与边界

- 起点：`5bf7398`，包括刚更新的 ZzPureTools `d8afb260`；清理前补交 `eaec438`，同步依赖布局检查中的固定提交号。
- P2 只清理有引用证据的失效项、补齐缺失的一级依赖记录。所有依赖的来源、许可和本地补丁逐项审计仍保留在总路线中。
- 不改 Markdown 渲染、通知节流、搜索或压缩实现；这些任务需要各自的行为基线。
- 删除的文件可通过 Git 历史恢复，不重写历史，不据此声称安装包或运行速度改善。
- 延续本会话的主线集成安排，按任务提交；推送留待用户安排。

## P0：记录本批基线

- [x] 确认工作区改动只有已编写的优化路线和文档索引。
- [x] 保存配置并重新运行 `ci_build`、CTest；先通过再删除依赖。
- [x] 提交总路线、文档索引和本执行计划。

环境：Ubuntu 26.04.1 x86_64，Intel Core i7-14700（28 个逻辑 CPU），GCC 15.2.0，CMake 4.3.3，Qt 6.11.1。`out/build/audit-fixes` 使用 Release、LTO ON、WARNINGS_AS_ERRORS ON、单元/集成/UI 测试 ON、Hyperscan/VectorScan OFF。编译并行度 12，CTest 并行度 8；应用运行线程数保持默认，未进行吞吐测量。

```bash
cmake --build out/build/audit-fixes --target ci_build --parallel 12
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/audit-fixes --output-on-failure --parallel 8
```

## P1：移除 whereami

**文件：** `3rdparty/CMakeLists.txt`、`src/utils/CMakeLists.txt`、`3rdparty/vendor/README.md`、`NOTICE`；删除 `3rdparty/vendor/whereami/`。

- [x] 用 `git grep` 检查 `whereami` 和 `wai_get`，区分库自身、历史说明和实际调用。
- [x] 删除 `add_library(whereami STATIC ...)`、其 include 目录、`klogg_cpm_targets` 中的 `whereami` 和 `klogg_utils` 的链接项。
- [x] 删除 vendor 目录、当前依赖表项和 NOTICE 中仅属于 whereami 的段落。
- [x] 在全新构建目录配置并构建 `ci_build`，运行全部 CTest。仅复用已下载的 KDE 源码，不复用构建对象。
- [x] 记录结果，提交 `chore(deps): remove unused whereami (P1)`。

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-release \
  -B out/build/dependency-cleanup \
  -DKLOGG_BUILD_TESTS=ON -DWARNINGS_AS_ERRORS=ON -DKLOGG_USE_LTO=ON \
  -DFETCHCONTENT_SOURCE_DIR_ZZLOGG_ECM="$PWD/out/dependency-downloads/extra-cmake-modules-6.22.0" \
  -DFETCHCONTENT_SOURCE_DIR_ZZLOGG_SYNTAX="$PWD/out/dependency-downloads/syntax-highlighting-6.22.0"
cmake --build out/build/dependency-cleanup --target ci_build --parallel 12
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/dependency-cleanup --output-on-failure --parallel 8
```

预期配置、编译、链接成功，所有已注册测试通过，包括应用启动、重启路径和 staged install。无需增加断言库名不存在的源码扫描测试。

## P2（部分）：清理历史记录和停用工具

**文件：** `3rdparty/vendor/README.md`、`.github/actions/agent-package-win/action.yml`、`.github/workflows/ci-build.yml`；删除 `packaging/windows/codesign_client.exe`。

- [x] 删除不存在的 backward-cpp、sentry-native 表项；补充 Monocypher、nlohmann-json、ZzPureTools，以及 KDE 下载依赖的入口和固定版本。
- [x] 检查打包脚本、测试、现行文档与工作流对签名客户端的引用。
- [x] 删除停用的客户端和被注释的三个签名步骤；同时删除该 action 的三个未使用 S3 inputs 及调用方的对应 `with` 参数。
- [x] 保留 `StageRuntimeLicenses.cmake` 的有效许可证清单和现行签名脚本。
- [x] 运行打包/安装契约测试、Python CI 测试，检查本地 Markdown 链接及 diff。
- [x] 记录结果，提交 `chore(packaging): remove obsolete signing client (P2)`。

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/dependency-cleanup \
  --output-on-failure -R 'packaging|installer|runtime|install|release|dependency'
python3 -m unittest discover -s tests/ci
git diff --check
```

预期测试全部通过；Python 中依赖原生 Windows 的跳过项单独报告。Windows/macOS 原生编译、打包与签名需后续 CI 验证。

## 结果记录

清理前 Git 跟踪文件：whereami 26 个文件、75,348 字节；旧签名客户端 1 个文件、12,989,440 字节。合计 13,064,788 字节（约 12.46 MiB），不含其他文本变化，也不代表 Git 历史体积减少。

### P0 验证

2026-11-13：`ci_build` 成功，原有 LTO 构建仍输出 `-Wodr` 警告；未在本批扩大处理。初次 CTest 为 91/92，依赖布局检查使用旧 pin，已在 `eaec438` 修正。复跑全部 CTest 为 92/92，通过耗时见本机 `/tmp/zzlogg-p0-ctest.log`。本批只建立构建与安装行为基线，搜索性能和 P3/P4 行为基线尚未开始。

### P1 验证

2026-11-13：全新 `out/build/dependency-cleanup` 配置和 `ci_build` 成功，CTest 92/92（13.36 秒）。新构建图无 whereami，启动、重启路径和 staged install 测试通过。配置与 P0 的关键选项一致；此结果不用于性能比较。Windows/macOS 原生验收未执行。

### P2 验证与提交

2026-11-13：清理提交 `75fdec6`；P1 提交 `ca5d20d`。19 项打包、安装、运行时、发布和依赖相关 CTest 全部通过，Python CI 运行 124 项、跳过 3 项，其余通过。使用 YAML 解析比较清理前后的 Windows 打包 action，三个有效步骤完全一致，调用方不再提供已删除的 S3 inputs。依赖表与 22 个实际 vendor 目录一致，本地文档链接均有效，同时修正了 CRoaring 的历史失效链接。

本批未修改现行签名脚本、有效许可证收集清单或 Git 历史。Windows/macOS 原生构建与打包未执行；提交尚未推送。P2 的全量来源、许可及本地补丁审计仍待后续完成；建议下一批先记录三语手册的行为，再开展 P3 的 Qt Markdown 迁移。
