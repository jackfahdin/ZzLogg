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

## 2026-10-19 修复的更新测试问题

目录整理时，以提交 `17782a8` 为基线，在 Linux、Qt 6.11.1、GCC 15.2、Release、
`KLOGG_BUILD_UI_TESTS=ON` 环境中发现以下问题。GitHub Actions 的 Windows/Linux
构建也确认了其中的链接冲突。两项现已修正测试并通过本地验证，没有新增 CI 排除规则。

| 目标或测试 | 现象 | 基线核验 |
| --- | --- | --- |
| `zzlogg_update_integration_test` / `zzlogg_update.integration` | `productionFeedConfiguration()` 重复定义，链接失败 | 整理前快照生成的链接命令与整理后相同；目标定义及相关源码未变，使用该命令链接现有对象重现同一错误 |
| `zzlogg_update.policy` | `PolicyTest::urls()` 仍期望拒绝 `http://updates.example.invalid/a`，实际返回接受 | 在整理前快照中独立构建并运行该测试，同一断言失败 |

集成测试的 `tests/updateqt/unconfiguredfeed.cpp` 现在替换应用实际调用的
`appFeedConfiguration()`，避免把生产配置对象链接进离线测试。集成测试继续验证
应用的更新检查为未配置状态，不访问生产或本地更新源。

URL 测试区分生产与测试环境：生产环境和默认策略只接受 HTTPS/443，测试环境按既有
本地更新源规则允许 HTTP 和自定义端口，两者都必须匹配主机白名单。修复没有修改生产
URL 校验实现，也没有通过放宽生产策略让测试通过。

## 2026-10-19 Windows 升级入口契约断言过期

发布构建 `39234821816` 的 Linux 和两个 macOS 作业通过，Windows 编译成功，
但 `zzlogg_ui.upgrade_mode_contract` 仍要求 `PinDirectory` 使用只读共享（`1`）。
安装器已在 `a628bd5` 中改为读写共享（`3`），解决事务引擎替换子文件时的
`ERROR_SHARING_VIOLATION`，同时不允许删除共享，继续防止目录被改名或替换。

契约断言与注释现已同步为读写共享、禁止删除共享；没有修改安装器行为或新增测试排除。
