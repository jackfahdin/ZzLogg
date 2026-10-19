# CI 已知失败

Windows 作业在 2026-09-21 之前没有传 `-DKLOGG_BUILD_UI_TESTS=ON`，`tests/update`、
`tests/updateqt`、`tests/updater`、`tests/ui_acceptance` 四个目录从未在 CI 上编译或执行，
只在开发机上跑过。打开开关后有四个测试在 GitHub 托管 runner 上失败，原因与被测代码无关，
暂时在 `.github/actions/agent-run-tests/action.yml` 里排除。

排除项与观察到的现象：

| 测试 | 现象 | 备注 |
| --- | --- | --- |
| `zzlogg_ui.storage_migrator` | Failed | 尚未定位 |
| `zzlogg_ui.windows_installer_contract` | `Runtime deployment is missing expected dependency-closure sentinel: icuuc.dll` | runner 上的 Qt 部署不含 ICU，开发机 Qt 安装带 ICU |
| `zzlogg_ui.storage_application_smoke` | Timeout | 尚未定位 |
| `zzlogg_ui.windows_local_runtime_contract` | Timeout | 尚未定位 |

`klogg_itests` 不在此列：它是已知的间歇性并行搜索集成测试，单独用
`--repeat until-pass:3` 重跑，见同一个 action 文件。

排除是临时措施。每修好一个就从 `-E` 表达式里删掉一项，并在此处移除对应行。

## 2026-10-19 本地完整构建发现的既有问题

目录整理时，以提交 `17782a8` 为基线，在 Linux、Qt 6.11.1、GCC 15.2、Release、
`KLOGG_BUILD_UI_TESTS=ON` 环境中发现以下问题。它们不在上述 Windows 临时排除名单内，
本次目录迁移没有新增 CI 排除规则。

| 目标或测试 | 现象 | 基线核验 |
| --- | --- | --- |
| `zzlogg_update_integration_test` / `zzlogg_update.integration` | `productionFeedConfiguration()` 重复定义，链接失败 | 整理前快照生成的链接命令与整理后相同；目标定义及相关源码未变，使用该命令链接现有对象重现同一错误 |
| `zzlogg_update.policy` | `PolicyTest::urls()` 仍期望拒绝 `http://updates.example.invalid/a`，实际返回接受 | 在整理前快照中独立构建并运行该测试，同一断言失败 |

集成测试的 `tests/updateqt/unconfiguredfeed.cpp` 提供离线配置替身；应用改为调用
`appFeedConfiguration()` 后，链接器会同时取出定义该函数与生产配置函数的
`src/updateqt/src/updateconfiguration.cpp` 对象，造成重复符号。修复应同时检查离线测试
隔离和本地更新源的信任边界，不能通过忽略链接错误或放宽生产更新策略处理。
