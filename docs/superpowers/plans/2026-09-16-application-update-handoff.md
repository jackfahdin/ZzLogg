# 安装版更新 3B.4：应用准备与交接接线实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development 逐任务实现此计划。步骤使用复选框跟踪进度。

**目标：** 把可取消的会话退出准备、同安装目录入口协调和受限更新交接接入应用，仍不开放真实安装。

**架构：** 保留主窗口直到会话同步和真实握手完成；准备状态与提交关闭分离。Windows 原生目录协调提供应用侧动态 CRT 和更新器静态 CRT 两个同源闭包。Qt 交接控制器异步使用原生协调接口，产品能力门禁保持关闭，真实链路仅由测试目标组合。

**技术栈：** Qt 6、C++17/20、Windows x64/MSVC、CMake、Qt Test、CTest。

**规格：** `docs/superpowers/specs/2026-09-16-installer-handoff-design.md` 第 4–6 节及 `2026-09-15-installer-only-update-design.md`。

## 全局约束

- 仅 Windows x64 安装版、全量 NSIS 包、用户主动更新、生产入口默认关闭。
- 本期不启动真实安装器、不请求 UAC、不写 HKLM、不修改真实安装目录；不得通过环境变量、CLI、Qt 动态属性开放测试安装成功后门。
- 测试成功不替代发布者策略、管理员独立验证、真实 UAC、跨账号或持久恢复验收。生产服务仍无正式发布身份，安装按钮默认隐藏。
- 主程序为动态 CRT；ZzLoggUpdate.exe 整个链接闭包保持静态 CRT、无 Qt。共享源文件不等于混合链接两种 CRT。
- 保留普通退出、重启、托盘、单实例转发、--multi、多窗口和数据定位现有行为。失败/取消不得关闭窗口，不按进程名终止实例。
- 先运行失败测试再实现；Qt 失败日志在复跑之前归档。修改按任务中文标题和详细中文正文提交；最后快进合并 master 并重编译测试，不 push。
- 不测试其他操作系统；非 Windows 构建保留无更新能力的编译边界。保留用户未跟踪文件和子模块。

## 文件职责

- `src/app/kloggapp.h`、新增 `src/app/applicationexitpreparation.*`（如抽取必要）：拥有退出准备状态、窗口快照及取消/提交。
- `src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`：窗口准备期间限制变更及关闭，恢复原状态。
- `src/updater/installlock_win.*`、新增 `installationactivity_win.*`：目录身份、入口租约、独占更新预留；不涉及安装授权。
- `src/app/applicationrunner.cpp`、`klogg_grep.cpp`、新增 `applicationupdateguard.*`：在转发/存储/窗口创建前检查目录协调，持有应用活动租约。
- 新增 `src/app/applicationupdatehandoff.*`：Qt 生命周期、异步受限协调及取消；不让主线程执行有界管道等待。
- `src/ui/include/updatecheckdialog.h`、`src/ui/src/updatecheckdialog.cpp`、`src/app/i18n/*.ts`：退出更新/准备/取消的界面状态与三语文案。
- `src/updater/CMakeLists.txt`、`src/app/CMakeLists.txt`、运行目录脚本：两种 CRT 闭包和正式更新器部署。
- `tests/ui_acceptance/*`、`tests/updater/*`：真实窗口、真实进程、临时目录与部署产物验收；fixtures 不部署。

## 任务 0：先排查协调者基线异常

**文件：** `tests/updater/coordinatortest.cpp` 及由堆栈或可复现证据直接指向的 `src/updater` 文件；不扩展到其他功能。

- [x] 保存 `out/ui-vs/3b4-baseline-tests.log`（98/99，coordinator 0xc0000409），用聚焦运行/重建/系统事件或调试器识别异常位置；原始失败没有可见断言，不能直接归因于超时。
- [x] 若发现代码缺陷，先建立定向可运行 RED，再最小修复；若为旧二进制/环境问题，记录对比证据，不无依据改代码。不能用重试或放宽断言宣称修复。
- [x] 聚焦及全套验证，中文提交有证据的修复（若有），报告根因与日志。

```powershell
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R '^zzlogg_updater.coordinator$' -V
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target zzlogg_updater_coordinator_test --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
```

## 任务 1：可撤销的应用退出准备

**文件：** `src/app/kloggapp.h`；`src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`；`tests/ui_acceptance/restartcontracttest.cpp`；必要时抽取 `applicationexitpreparation.*` 并更新 app/tests CMake。

- [x] 在现有真实 KloggApp 测试中先添加准备不关闭、失败回滚和取消恢复测试；运行证明旧实现缺失。接口为 `prepareApplicationExit()`、`cancelApplicationExitPreparation()`、`commitApplicationExit()`、`isApplicationExitPrepared()`，具体可返回 bool 与保留生命周期令牌，但不暴露测试专用 setter。

```cpp
QVERIFY(app.prepareApplicationExit());
QVERIFY(first->isVisible()); QVERIFY(second->isVisible());
QCOMPARE(closed.count(), 0);
QVERIFY(!first->close()); // 准备等待期间不得绕过提交关闭
app.cancelApplicationExitPreparation();
QVERIFY(first->isEnabled());
QVERIFY(!app.isApplicationExitPrepared());
```

- [x] 保存所有窗口并同步 session QSettings 成功后才进入 Prepared，窗口不关闭；准备失败取消已经准备的窗口并恢复原 session exitRequested；无窗口安全处理，重复准备拒绝，取消幂等，提交必须要求准备状态。
- [x] Prepared 期间保留窗口但冻结会改变快照的操作：窗口交互、创建窗口、打开文件/拖放/IPC、托盘操作、关闭与普通退出/重启均不得绕过；保留并恢复此前 enabled 状态。非交互文件请求可拒绝并诊断，不默默载入改变保存快照。后台跟随可继续读取，但不能改已保存快照。
- [x] 以 QPointer 保存准备窗口集合并检查窗口丢失；任何提交前失败均撤销准备。正常退出/重启复用 prepare+commit；正确绕过托盘关闭，既有重启契约保持。提交关闭不得在第一个窗口关闭之后才发现可预检的拒绝。
- [x] 真实多窗口测试涵盖准备失败、同步失败、取消、重复调用、窗口原本 disabled、等待期间 close/newWindow/loadFile 被阻止、提交保留会话及普通重启回归；不新增生产测试开关。至少一个临时守卫变异必须使测试失败，恢复后通过。
- [x] 聚焦构建与测试，完整 Release/CTest，自审后提交。

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'restart_contract' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 拆分应用退出准备与提交阶段" -m "3B.4 任务一：保存同步后保留窗口，冻结快照变更，取消恢复，并保留普通退出与重启行为。"
```

## 任务 2：同目录实例协调与所有启动入口

**文件：** `src/updater/installlock_win.*`、新增 `installationactivity_win.*`、updater CMake；根 `CMakeLists.txt` 与 `src/CMakeLists.txt` 调整子目录顺序；`src/app/applicationupdateguard.*`、`applicationrunner.cpp`、`klogg_grep.cpp`、app/tests CMake；`tests/updater/installationactivitytest.cpp`。

- [x] 先写真实临时目录及子进程的进入/预留失败测试。新增 RAII `InstallationActivity` 提供 `enter(root)`、`reserveUpdate()`、`cancelUpdate()`：多个正常实例可并存；更新预留与进入使用 3B.3 相同目录身份 mutex，检查到进入之间无放行窗口；同目录其他实例阻止预留，其他目录不受影响。

```cpp
InstallationActivity first, second;
CHECK(first.enter(root) == ActivityError::None);
CHECK(second.enter(root) == ActivityError::None);
CHECK(first.reserveUpdate() == ActivityError::Blocked);
// second 在真实子进程结束后销毁；随后 first 能预留。
// 预留存续时新子进程 enter(root) 必须失败，取消后正常进入。
```

- [x] 使用操作系统句柄生命周期实现只读活动租约，不往 Program Files 创建用户锁文件。可用目录共享读句柄表示实例存在，在持有更新 mutex 时撤下本实例活动句柄并独占探测目录，确认没有其他活动读句柄后恢复稳定租约并保持更新 mutex；必须避免自身 InstallLock 路径句柄造成自冲突，并保留目录身份、祖先反替换保护。用真实实验验证共享语义，不能仅凭 flags 推论正确。若无法安全支持该方案，先报告控制者，不换成无证明的注册表/进程名枚举。
- [x] 取消/失败恢复本实例租约后才释放更新 mutex；句柄获取失败和异常身份不允许更新。其他同目录实例保守提示先自行关闭，不主动退出其他实例，更不强杀；未知旧实例/文件占用仍由后续安装端独立检查，不能宣称已证明可覆盖全部文件。
- [x] GUI 入口在单实例转发和 --multi 分支之前进入活动租约，grep 在存储引导之前进入；转发进程也必须尊重更新预留。普通支持目录的应用活动租约持续到真实应用进程收尾。仅不支持自动更新的路径形态（例如网络路径）允许保留手动使用且能力关闭；已观察到更新锁、权限失败或身份异常不视为不支持而放行。引导错误不启动存储选择器。
- [x] 应用侧为 KDSingleApplication 提供按安装目录隔离的实例名（不修改 vendor）：现有默认仅取 EXE 文件名，会把 B 目录请求转发到正在更新的 A 目录。保持同目录转发、不同目录独立；目录身份/路径大小写归一化可复核，真实进程测试不能仅比较生成的 key 字符串。
- [x] 原生源同源创建应用 `/MD` 与 updater `/MT` 两个闭包，Qt 不链接 MT 库。新的启动保护没有 Linux/ARM 自动更新支持承诺。测试真实子进程同/异目录、多实例、更新期间新启动、取消恢复、崩溃释放、祖先替换；显式编译 grep，但不加入默认目标。
- [x] 完整 Release/CTest、相关 PE/CRT 检查，自审中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target klogg_grep --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 接入安装目录活动租约与启动保护" -m "3B.4 任务二：协调同目录多实例与更新预留，在单实例转发和独立入口前检查，保持两类 CRT 闭包隔离。"
```

## 任务 3：异步交接界面与正式更新器部署

**文件：** `src/app/applicationupdatehandoff.*`、`kloggapp.h`、app/updater CMake；`src/ui/include/updatecheckdialog.h`、`src/ui/src/updatecheckdialog.cpp`、三语 ts；runtime staging 脚本；`tests/ui_acceptance/updatehandofftest.cpp`、`updatecheckuitest.cpp`、相关 CMake 与 `docs/development/UPDATE_PROTOCOL.md`。

- [x] 新 Qt 控制器按 Idle→Preparing→Waiting→ExitCommitted 及 Cancelled/Failed 管理生命周期；UI 只消费枚举快照，执行可用性由生产后端给出且本期恒关闭，不由 Verified 下载状态推导。失败测试先证明未授权/只 Ready/启动失败均不能提交退出。

```cpp
// 使用测试目标专属原生 fixture，不在生产入口增加成功开关。
QVERIFY(controller.begin());
QTRY_VERIFY(controller.isWaiting());
QVERIFY(window->isVisible());
controller.cancel();
QTRY_VERIFY(window->isEnabled());
QCOMPARE(closed.count(), 0);
// 正确真实进程 AwaitingAppExit 后才可调用 commitApplicationExit。
```

- [x] 控制器依赖任务 1 准备 API 和任务 2 目录活动预留；先准备/同步，再预留同目录，然后启动已有原生 Coordinator（应用 MD 闭包）。Qt 主线程不阻塞在 connect/read/write/进程等待。异步消息使用对象生命期和单次代次隔离，取消后迟到成功不能退出；销毁恢复准备并安全释放，原生运行副本租约仍覆盖真实子进程结束。
- [x] 只有原生协调者已认证且存活 AwaitingAppExit 才提交退出。测试成功链在独立测试进程运行 QApplication，不能拿假的 bool 替代真实握手；生产后端仍拒绝 begin，不执行安装。保存失败、取消、启动失败、拒绝/超时、对端提前退出、迟到完成、重复点击都保持原窗口可用。UAC 拒绝仅测错误映射，不声称已做真实 UAC。
- [x] 预留必须跨越主程序真实退出：扩展内部 bootstrap 传递可选的目录身份，子协调者在握手前以 SYNCHRONIZE 打开并持有该身份对应的既有 mutex，父进程仍持有原句柄时完成绑定。现有 InstallLock 对任何已存在对象均拒绝，因此该观察句柄在父退出后仍阻止新进入，直到子协调者结束；不跨线程/跨进程移动 mutex 所有权，不把句柄存在当成安装授权。真实子进程测试父退出后新入口仍被拒绝、子结束后重新允许；未启用目录预留的原有独立协议测试保持受限内部用法。需要修改 `src/updater/bootstrap_win*`、`coordinator*` 和相应 fixtures/测试。
- [x] 更新对话框新增受能力约束的“退出并更新”及准备/等待/取消状态；生产能力关闭时按钮隐藏，不能发可执行请求。准备期间检查、下载、跳过等会改变选择的动作停用；关闭/ESC 等价取消。便携副本仅手动更新提示。英文/简体/繁体运行时切换及 150% 布局自动验收，长文本不遮挡按钮。
- [x] 通过正式 CMake target 将 ZzLoggUpdate.exe 加入安装和 Windows runtime-folder 产物，应用构建依赖 helper；不扫描测试输出，不部署 fixtures。运行目录验证 helper 无 Qt/MSVC 动态运行库依赖，入口仍拒绝任意参数。不得为了演示更改生产签名/正式身份门禁。
- [x] 更新文档明确受限接线、目录实例的保守阻塞策略、仍缺管理员事务/生产配置/真实升级验收；完整测试、原生链路、Qt 三语和部署产物检查，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target zzlogg_runtime_folder --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 接入可取消的更新交接界面" -m "3B.4 任务三：异步串联会话准备与受限协调，增加三语状态并部署独立更新器，生产安装保持关闭。"
```

## 验收与交付

- [ ] 每任务独立审查、最终整阶段审查，主控重新构建/测试。
- [ ] master 快进合并、主线 Release 构建与测试，记录程序位置；不推送。
- [ ] 明确测试环境边界和下一阶段 3C 受保护安装事务，不宣称自动更新已上线。

## 执行记录

- 起点 master `40aa4255`（3B.3 主线验收完成），隔离工作树 `.worktrees/update-core`、分支 `codex/installer-update-plan`；基线 98/99，协调者测试 0xc0000409 无可见断言，证据 `out/ui-vs/3b4-baseline-tests.log`。用户已授权验证完成后自动快进合并 master，不 push。
- 计划提交 `0e01d720`；起飞前裁决：同目录其他实例保守阻塞并提示手动关闭，不实现跨进程强关；KDSingleApplication 实例名按安装目录隔离；子协调者以 SYNCHRONIZE 观察句柄延续目录预留跨越主程序退出。
- 任务 0：基线异常根因是协调者测试复用进程临时目录；`5cd5e4f6` 仅修测试（唯一 nonce 加原子创建），定向 RED 复现后转绿，完整 99/99（105.29 秒）。独立审查无发现。
- 任务 1：`916ffa20` 拆分退出准备与提交，真实 Qt 聚焦 15/15、两次守卫变异均失败、恢复后完整 99/99（110.42 秒）。独立审查规格符合、质量通过，无关键/重要发现；次要项记入账本留待最终审查甄别。
- 任务 2：`e3973f75` 接入 InstallationActivity 目录活动租约、GUI/grep 启动入口保护与按目录隔离的单实例名，建立应用 /MD 与更新器 /MT 同源双闭包。真实子进程与共享语义实验验证（含 share=READ 阻断目录内文件创建的意外回归，活动租约改为 READ|WRITE 并固化断言；InstallLock 语义未放松），三组变异均被捕获，完整 103/103 通过。独立审查规格符合、质量通过；首轮失败日志归档缺失已补录（task-2-first-full-failure.log，含 provenance），偏离"share-read"表述经控制器裁决接受。
- 任务 3：`b9ef658b` 接入可取消的更新交接界面与正式更新器部署。控制器代次/生命期隔离、bootstrap v2 观察句柄延续目录预留跨越主程序真实退出、对话框能力门禁与三语状态、ZzLoggUpdate.exe 进入安装与 runtime-folder 产物；三组变异均被捕获，完整 105/105 通过（116.58 秒）。过程发现：QTRY_VERIFY 重复求值副作用表达式（测试改为显式重试）、取消后子进程观察句柄短暂维持保守阻塞（设计内）；一次负载异常窗口出现 restart_contract 300 秒挂起 0xc0000409 与 storage_migrator 符号链接用例失败，单跑与复跑均通过，记录待最终审查甄别。
