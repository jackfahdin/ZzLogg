# 审查问题修复计划

**目标：** 修复 2026-10-20 审查确认的七项发布、更新及日志界面问题。

**实施原则：** 每项先写能重现故障的测试，确认失败后做最小修复；每项独立验证，最后运行完整 Linux 构建与测试。Windows 和 macOS 的原生运行验收单独记录，不把交叉编译当作原生验证。

## 第一阶段：发布完整性

- [x] **1. 修复预览版 Release 刷新中断后的跳过判断。** 修改 `scripts/ci/publish_release.py`、`tests/ci/test_release.py`。先让第二次附件上传中断，断言公开 Release 不会被当成完整快照；再让重试恢复全部八个附件。保留现有 Release 排序要求。验证：`python3 -m unittest discover -s tests/ci -p test_release.py`。

## 第二阶段：日志界面

- [x] **2. 关闭搜索结果页时释放数据。** 修改 `src/ui/src/crawlerwidget.cpp` 和 UI 测试。测试连续创建、关闭结果页后，关闭页对应的数据不再由映射持有；修复销毁回调中的类型转换和索引移除顺序。验证相应 UI 测试。
- [x] **3. 清除标记时同步过滤结果。** 修改 `src/logdata/src/logfiltereddata.cpp`、`tests/ui/logfiltereddata_test.cpp`。测试 `clearMarks()` 后标记数和可见行数都减少；修复 `marks_and_matches_` 的同步。验证相应单测。
- [x] **4. 保留后台标签的加载失败状态。** 修改 `src/ui/src/crawlerwidget.cpp` 和 UI 验收测试。让非活动标签加载失败后切回，断言窗口处理失败而非成功；保存并重放实际加载状态。验证文档工作区测试。

## 第三阶段：平台更新

- [x] **5. Windows 安装更新失败后正确恢复旧版。** 修改 `src/updater/relay.cpp`、相关 updater 测试。让事务失败发生在应用退出后，断言重启前两个锁观察句柄已释放、原 `--data-dir` 被传回；成功路径保持不变。验证 Windows 交叉编译和可运行时的原生测试。
- [x] **6. Windows 便携更新重启保留数据目录。** 修改 `src/portableupdater/portableupdateinstaller.cpp`、`src/portableupdater/helper_win.cpp` 和对应测试。把当前有效数据根目录传给 helper，由 helper 安全地作为 `--data-dir` 重启参数传回。验证 Windows 交叉编译和可运行时的原生测试。
- [x] **7. macOS 设置通道与 Sparkle feed 一致。** 修改 `src/app/macupdatecontroller.mm`、`src/app/kloggapp.h` 及对应平台测试。设置 Stable/Preview 后更新 Sparkle 的有效 feed，并拒绝旧通道检查结果。macOS 原生构建和测试仍待执行；Linux 仅覆盖非 Sparkle 分支及 feed 元数据测试。

## 收尾

- [x] Linux `ci_build`（`WARNINGS_AS_ERRORS=OFF`）、离屏 CTest 92/92、CI Python 测试 117 项（3 项跳过）通过，`git diff --check` 无误。严格构建被现有 GCC 15 `src/portableupdater/transaction.cpp:45` 的 `sign-conversion` 警告阻断。
- [x] 原生验收待执行：Windows 上构建并运行 `zzlogg_updater.relay_fixture`、`zzlogg_updater.relay_production`、`zzlogg_portable.installer`、`zzlogg_portable.helper`；macOS 上构建并运行 `zzlogg_update.mac_exit`，再手动切换 Stable/Preview 并确认 Sparkle 请求相应架构的 feed。Linux 上的 Windows 交叉编译不能代替原生运行，macOS Sparkle 控制器未在 Linux 编译。
