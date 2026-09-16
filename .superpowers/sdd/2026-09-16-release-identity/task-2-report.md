# 3B.1 任务二执行报告：组合安装与发布身份

## 结果

- 新增 `zzlogg::updateqt::makeInstalledRelease` 纯组合函数。只有正式发布字段再次通过校验、安装身份为 `Registered` 且根目录非空、调用方提供的 OS 主版本非零时，才返回 `Distribution::Installer` 的 `InstalledRelease`。
- 返回值逐字段复制版本、完整 uint64 发布序号、渠道、平台、架构、完整 uint32 data schema、协议版本与调用方 OS 版本，并明确设置 `developmentBuild=false`。
- 未接入 `KloggApp`：检查服务和下载服务仍传入 `std::nullopt`。未新增 OS 探测、编译配置开关、安装器执行、UI/下载策略修改或 HKLM 写入。
- 更新 `docs/development/UPDATE_PROTOCOL.md`，记录正式发布字段、默认关闭行为、组合条件，以及正式身份不构成安装授权；明确 3B.2–3B.4/3C 未完成，data schema 0 只能显式声明且不代表现有配置文件已有统一 schema。

## TDD 红灯

先只添加 `tests/updateqt/installedreleasetest.cpp` 及测试目标注册，运行：

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target zzlogg_update_installed_release_test --parallel 8
```

结果：exit 1。编译在 `installedreleasetest.cpp(3,10)` 以 `C1083` 失败，原因是预期接口 `zzlogg/updateqt/installedrelease.h` 尚不存在；失败来自缺少生产功能，不是夹具或运行环境错误。

## 绿灯与聚焦验证

加入最小头文件、实现和库接线后运行：

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target zzlogg_update_installed_release_test --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R '^zzlogg_update\.installed_release$' --output-on-failure
```

结果：构建 exit 0；CTest 1/1 通过、0 失败。最终 QtTest 明细为 23 passed、0 failed、0 skipped、0 blacklisted，包括：

- 完整成功结果的所有字段，特别是 Installer、`developmentBuild=false` 和实际 OS `10.0.22631`；
- stable/preview、schema 0/UINT32_MAX、UINT64_MAX 发布序号；
- 空发布身份、所有四个非 Registered 枚举、空根、空/未知渠道、错误 OS/架构；
- year/month/patch 无效版本、零序号、协议 0/2、OS 主版本零。

QtTest 输出：`out/ui-vs/tests/updateqt/installed_release-Release.txt`。

## 变异与恢复

临时移除生产实现中的 `installation.kind != InstallationKind::Registered` 门禁，重新构建并执行聚焦测试。

结果：CTest exit 1，0/1 目标通过；QtTest 为 19 passed、4 failed。失败行精确为 `unregistered`、`legacy`、`invalid-installation`、`unsupported-installation`，均在拒绝断言处失败，证明测试能捕获 Registered 门禁缺失。

恢复门禁后再次运行同一聚焦命令：CTest 1/1 通过，QtTest 恢复为 23 passed、0 failed。变异未保留在工作树。

## 最终完整验证

按计划只执行一次本阶段 Release 完整构建和全套 CTest：

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
```

- Release 完整构建：exit 0，新增库、测试目标及 `ZzLogg.exe` 均成功链接。
- 全套 CTest：exit 0，84/84 通过，0 失败，总耗时 89.42 秒。
- 为避免后续复验覆盖 `LastTest.log`，本次完整日志保留在 `out/ui-vs/task-2-full-ctest.log`（77,454 字节）。

## 变更文件

- `src/updateqt/include/zzlogg/updateqt/installedrelease.h`
- `src/updateqt/src/installedrelease.cpp`
- `src/updateqt/CMakeLists.txt`
- `tests/updateqt/installedreleasetest.cpp`
- `tests/updateqt/CMakeLists.txt`
- `docs/development/UPDATE_PROTOCOL.md`
- `.superpowers/sdd/2026-09-16-release-identity/task-2-report.md`

## 疑虑与边界

- CMake 重新配置仍报告仓库既有第三方弃用/策略与可选依赖探测提示；本任务新增 C++ 未报告编译警告，未扩大范围处理既有提示。
- 纯函数要求调用方提供真实 OS 版本；本阶段刻意不探测系统，也不将测试 OS 版本接入应用。
- 正式构建身份不是签名证明或安装授权。执行前重验、稳定文件交接、独立更新进程、UAC/NSIS、UI 接线和受保护安装事务仍由后续 3B.2–3B.4/3C 完成。
