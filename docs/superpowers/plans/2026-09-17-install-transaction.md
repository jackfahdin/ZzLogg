# 受保护安装事务（3C）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development 逐任务实现此计划。步骤使用复选框跟踪进度。

**目标：** 交付暂存验证、差分文件集与登记备份替换、追加式持久日志与幂等恢复、普通权限重启新版的受限安装事务链路；生产入口保持关闭，真实环境验收归阶段 4。

**架构：** NSIS 受限升级入口负责 UAC、目标独立核对与受保护暂存，事务引擎（载荷内嵌原生 exe，静态 CRT，复用 src/updater 源闭包）执行事务与恢复；协调者保持原用户普通权限，经协议 v2 的 Proceed 消息闸门在应用真实退出后才放行文件操作，事务完成后以原用户身份重启新版并做有界启动确认。

**技术栈：** C++17、Windows API、NSIS、CMake、PowerShell（打包清单）、无 Qt 的原生测试、Qt Test（应用侧接缝）。

**规格：** `docs/superpowers/specs/2026-09-17-install-transaction-design.md`；继承 `2026-09-15-installer-only-update-design.md` 第 6 节与 `2026-09-16-installer-handoff-design.md` 的交接边界。

## 全局约束

- 仅 Windows x64；协调者与事务引擎及其全部 C/C++ 依赖使用静态 CRT，不依赖 Qt；应用 /MD 闭包不变。共享源文件不等于混合链接两种 CRT。
- 本机不弹真实 UAC、不写真实 HKLM、不修改真实安装目录、不运行真实 NSIS 安装包；NSIS 改动仅以静态契约测试验收。测试替身只编译进测试目标，不部署；不存在命令行/环境变量/Qt 动态属性的生产后门。
- 高权限代码不信任客户端传入的目标路径、文件列表或"已校验"标记；NSIS 段与引擎独立核对登记、发布身份与路径边界；令牌不进命令行或普通日志。
- 事务引擎只从安装包载荷经受保护暂存运行；受保护根管理员/系统写、已验证用户读；不在用户可写缓存目录承载高权限代码或恢复指令。
- 未知文件不删除、已修改受管理文件冲突即停止、不递归清空安装目录、保护全部已知数据根；普通失败自动逆序恢复，中断后恢复需明确 UAC 授权，禁止承诺断电无提示回滚。
- 保留普通退出、重启、托盘、单实例转发、--multi、多窗口与数据定位现有行为；失败/取消不得关闭窗口，不按进程名终止实例。
- 先运行失败测试再实现；Qt 失败日志在复跑之前归档。修改按任务中文标题和详细中文正文提交；审查及验证完成后快进合并 master 并重编译测试，不 push。
- 不测试其他操作系统；非 Windows 构建保留无更新能力的编译边界。保留用户未跟踪文件（.arts/、serach.png、serach.svg）与子模块。
- 在 `.worktrees/update-core` 工作（分支 codex/installer-update-plan）；生产门禁不变：缺生产证书策略/正式发布身份时执行入口保持关闭，本阶段不开放"退出并更新"的生产可见性。

## 文件结构

- `src/updater/handoffprotocol.{h,cpp}`：协议 v2，新增 Proceed 消息与状态迁移。
- `src/updater/bootstrap_win.{h,cpp}`、`bootstrap_win_p.h`：映射版本 3，可选有界数据目录字段。
- `src/updater/coordinator.{h,cpp}`、`coordinator_p.h`：CommitExit 后等待真实进程退出再发 Proceed；安装器启动、凭据文件、重启与启动确认组合。
- 新增 `src/updater/txjournal_win.{h,cpp}`：追加式持久日志、刷写、解析与幂等重放。
- 新增 `src/updater/txengine_win.{h,cpp}`、`txengine_main.cpp`：受保护事务目录、空间预检、差分文件集、备份/替换/登记事务与恢复入口；独立 `ZzLoggUpdateTx.exe` 目标（编译进安装包载荷，不单独部署到运行目录）。
- `src/updater/installationprobe` 复用 3A 只读探测语义；引擎内独立实现登记/路径核对（无 Qt）。
- `packaging/windows/ZzLogg.nsi`、`GenerateNsisManifest.ps1`（新增，与卸载清单同源生成）、CI 打包步骤：落地清单与受限升级/恢复入口。
- `src/app/applicationupdatehandoff.*`、`kloggapp.h`：会话 Request 扩展（安装包路径与选择上下文）、准备丢失外发通知、有界析构等待等 3B.4 移交项；生产构造仍无工厂。
- `tests/updater/`：协议/引导/日志/引擎/协调者原生测试与 fixture 扩展。
- `tests/ui_acceptance/`：交接链路 Qt 测试扩展、安装器静态契约与清单生成器实测。
- `docs/development/UPDATE_PROTOCOL.md`：协议 v2、事务与恢复边界、交接时序说明。

## 任务 1：协议 v2 Proceed 闸门与 bootstrap v3

**文件：** 修改 `src/updater/handoffprotocol.{h,cpp}`、`bootstrap_win.{h,cpp}`、`bootstrap_win_p.h`、`coordinator.{h,cpp}`、`coordinator_p.h`、`src/updater/CMakeLists.txt`；`tests/updater/protocoltest.cpp`、`coordinatortest.cpp`、`channeltest.cpp`、`handofffixture.cpp` 及相应 CMake。

- [x] 先写失败测试：Proceed 编解码、错误顺序（AwaitingAppExit 前 Proceed、重复 Proceed、终态后 Proceed）、错误事务/令牌 Proceed 全部拒绝；协调者在 CommitExit 后、应用进程真实退出前不发 Proceed（用 linger fixture 证明），退出后立即发。

```cpp
// MessageKind 增加 Proceed=8；线协议版本字节 1->2，两端同构建一致，旧版本拒绝。
// 状态机：客户端侧 ExitCommitted 后仅接受 Proceed -> 可执行事务；Proceed 之前任何文件修改
// 属于引擎违约（任务 3 引擎遵守，此处协议层只保证顺序与凭据）。
// Coordinator::commitExit() 后新增内部步骤：等待应用进程真实退出（协调者复制应用进程句柄，
// adopt 防 PID 复用），然后发送 Proceed；应用进程仍存活时 proceedIfExited() 返回 PeerRunning。
```

- [x] bootstrap 映射版本 2->3：`BootstrapData` 增加可选数据目录字段（固定容量 wchar 数组，建议 240 字符上限；空表示无）；打开时拒绝 version!=3、含控制字符、非绝对路径、超长未截断终止。版本 2 映射被拒绝。
- [x] fixture 扩展：等待 Proceed 模式（收到 Proceed 才 Complete）、proceed 前假装写文件的违约检测模式由测试侧断言顺序即可（fixture 记录消息序列）。既有全部协议/通道/协调者测试迁移到 v2/v3 并保持语义；未启用预留的独立协议测试保持受限内部用法。
- [x] 变异：移除 Proceed 顺序守卫与 bootstrap 版本检查各一次，必须触发失败并恢复。
- [x] 完整 Release 构建与 CTest，中文提交。

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 扩展交接协议 Proceed 闸门与数据定位引导" -m "3C 任务一：协议版本 2 新增 Proceed 消息，协调者在应用真实退出后放行事务；引导映射版本 3 携带受限数据目录，旧版本拒绝。"
```

## 任务 2：持久事务日志与受保护事务目录

**文件：** 新增 `src/updater/txjournal_win.{h,cpp}`；`tests/updater/txjournaltest.cpp`；updater/tests CMake。

- [x] 先写失败测试：追加-刷写-重开解析往返；截断/撕裂行、未知操作、错序序号、事务 ID 不匹配均拒绝恢复并重放为零操作；同一记录重放幂等。

```cpp
// Journal record: seq(uint64) | op enum | flags | paths(长度前缀,规范化) | sha256 | size |
// old registry values(可选)。每行二进制定长头+变长体，写后 FlushFileBuffers。
// TxJournal::open(root, txid) 独占创建事务目录（复用 NtCreateFile 原子语义与祖先钉住），
// 拒绝 reparse/网络根；append(...) 先写后刷；replay() 从首条有效记录顺序重建已执行集合。
// 受保护根的 ACL 主体可注入：生产为 Administrators/ SYSTEM 写+用户读；测试以当前用户充当
// 该主体并断言 DACL 语义，不在本机写真实 ProgramData。
```

- [x] 空间预检接口 `checkVolumeSpace(root, stagingBytes, backupBytes)`：不足返回明确错误，不开始事务。
- [x] 目录 ACL/独占/祖先保护复用 3B.3 既有原语（installlock_win_p.h 共享实现），不新造一套路径校验。
- [x] 变异：去掉写后刷写或序号校验，测试必须失败并恢复。完整构建/CTest，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.txjournal' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 建立安装事务持久日志与受保护目录" -m "3C 任务二：追加式日志先写后刷、撕裂与错序拒绝、重放幂等；事务目录独占创建且 ACL 主体可注入；卷空间预检不足即拒。"
```

## 任务 3：差分文件集、登记事务与幂等恢复引擎

**文件：** 新增 `src/updater/txengine_win.{h,cpp}`、`txengine_main.cpp`；`src/updater/CMakeLists.txt`（`ZzLoggUpdateTx.exe` 静态 CRT 目标，同源闭包复用）；`tests/updater/txenginetest.cpp`、`handofffixture.cpp` 引擎模式。

- [ ] 先写失败测试（全部在临时目录 + 注入注册表读写器）：新增/变更/删除差分正确应用；未知文件保留；已修改受管理文件冲突即停止且已应用部分自动回滚；登记旧值随恢复还原；中断后（杀掉子进程）恢复模式按 journal 幂等完成回滚，重复恢复为零操作。

```cpp
// Manifest: header(magic/version/count) + per-file {relpath(UTF-8 规范化), size, sha256}。
// TxEngine::run(request): 核对(登记/标记/路径/身份,独立实现无 Qt) -> 空间预检 ->
// backup(旧清单中将被改/删的文件+登记旧值) -> apply(复制新增/变更,删除旧-新差集,更新登记与标记/清单)
// -> 报告成功。每步先 journal 后操作。普通失败：逆序恢复本次改动。
// 恢复模式仅能由 NSIS 受限入口以明确授权调用（引擎 argv 模式 + 该模式自身不做目标核对以外的
// 任何新授权）；恢复只按 journal 逆序，不接受外部文件列表。
// 引擎进程是交接协议客户端：Hello -> 目标复核 -> AwaitingAppExit -> 等 Proceed -> 事务 ->
// Complete/Failed。Proceed 前任何文件修改 = 违约，协调者侧测试用违约 fixture 证明被拒绝。
```

- [ ] 哈希复用既有 monocypher 闭包（BLAKE2b/SHA-256 以 update core 现有选择为准，保持一致）；清单解析有界（条目数/路径长度上限），拒绝 `..`、绝对路径、重解析组件。
- [ ] 引擎不从用户可写目录运行：生产断言自身镜像位于受保护暂存（父链 ACL 校验），测试注入受保护根替身；该断言的失败路径有测试。
- [ ] `ZzLoggUpdateTx.exe` 目标仅打包进安装器载荷（任务 4 接线），不加入 runtime folder、不 install 到应用目录；PE 依赖检查无 Qt/MSVC 动态 CRT。
- [ ] 变异两处（删除冲突检测、跳过 journal 先写），失败并恢复。完整构建/CTest，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.txengine' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe -C Release --test-dir out/ui-vs --output-on-failure
git commit -m "feat: 实现差分文件集与登记的事务引擎" -m "3C 任务三：新旧清单差分、备份与替换按日志先写后做，普通失败自动逆序恢复，中断恢复幂等且需明确授权；未知文件不删、冲突即停。"
```

## 任务 4：落地清单（schema 2）与 NSIS 受限升级/恢复入口

**文件：** 修改 `packaging/windows/ZzLogg.nsi`、`packaging/windows/GenerateNsisUninstallManifest.ps1`（提取共享枚举逻辑）或新增 `GenerateNsisManifest.ps1`；`.github/actions/agent-package-win/action.yml`；`tests/ui_acceptance/windowsinstallercontracttest.cmake`、新增 `tests/ui_acceptance/upgrademodecontracttest.cmake`；`src/updateqt/src/installationprobe.cpp`（schema 2 + 落地清单校验）与 `installationidentity.cpp`、相应测试。

- [ ] 安装段增写 `.zzlogg-files.manifest`（打包期由 PowerShell 生成器随卸载清单同源生成，含每文件尺寸+SHA-256）；`UpdateIdentitySchema` 升为 2；卸载清单追加删除该文件；既有登记/卸载顺序契约保持。
- [ ] 探测侧：schema 1 判定为 Legacy（不自动升级）；schema 2 要求清单存在且格式有界有效，否则 Invalid。更新 3A 测试矩阵。
- [ ] NSIS 受限模式：`.onInit` 解析 `/ZzLoggUpgrade=<定位名>` 与 `/ZzLoggRecover=<事务目录名>`；升级模式禁止 `/D=`（先检测到 `/ZzLoggUpgrade` 即拒绝并存 `/D=` 的命令行，保持既有 `/D=` 检查顺序契约）；跳过交互页；独立核对登记/标记/路径/身份；创建受保护事务目录、提取载荷引擎与新版清单、启动引擎并传播退出码；恢复模式只允许对已存在事务目录调用引擎恢复模式。
- [ ] NSIS 不能本机运行：全部以静态契约测试验收（剥注释后的结构断言，沿用 windowsinstallercontracttest 模式），并为清单生成器写真实 PowerShell 执行测试（含 `$` 转义、哈希正确性、与卸载清单同源一致性）。契约必须断言：受限模式不读 `/D=`、不展示目录页、登记核对先于任何写入。
- [ ] CI 打包步骤：生成落地清单先于 makensis（与卸载清单同一顺序约束）；契约测试覆盖 action.yml 步骤顺序。
- [ ] 完整构建/CTest，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_ui\.(windows_installer|upgrade_mode)_contract' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 增加落地清单与 NSIS 受限升级入口" -m "3C 任务四：安装段写机器可读文件清单、引导 schema 升 2；新增受限升级/恢复模式，独立核对目标并禁止改换安装目录；静态契约与清单生成器实测验收，不本机运行 NSIS。"
```

## 任务 5：协调者生产链路与普通权限重启

**文件：** 修改 `src/updater/coordinator.{h,cpp}`、`main_win.cpp`、`bootstrap_win*`；`src/app/applicationupdatehandoff.*`、`kloggapp.h`（Request 扩展与生产装配注释）；`tests/updater/coordinatortest.cpp`、`handofffixture.cpp`、`tests/ui_acceptance/updatehandofftest.cpp`。

- [ ] 先写失败测试：协调者启动安装器（fixture 引擎）完整链：Hello→AwaitingAppExit→CommitExit→真实退出→Proceed→事务→Complete→以原用户身份重启 fixture GUI→启动确认。UAC 取消（ShellExecuteEx ERROR_CANCELLED）、启动失败、引擎 Failed、重启后进程早夭（无端点）均失败关闭且不提交安装。
- [ ] 凭据文件：协调者写当前用户私有临时文件（随机名、ACL 当前用户），受限开关只传定位名；引擎读取后删除；文件缺失/身份不符即拒。令牌不进命令行/日志的既有契约测试扩展覆盖该文件路径。
- [ ] 重启：Complete 后协调者复核 `<安装根>\ZzLogg.exe` 位于已登记目录且标记/清单一致，以原用户身份启动并携带 bootstrap v3 数据目录；有界等待进程存活 + 目录作用域单实例端点出现；确认失败报告并保留备份待授权恢复。父进程已提权返回 ManualRestartRequired（既有语义回归测试保持）。
- [ ] 应用侧：`CoordinationSession::Request` 增加安装包路径与选择上下文字段；生产构造仍无工厂、begin 恒拒（契约测试保持）；UI 不新增生产可见能力。
- [ ] 变异：删除重启前登记复核或启动确认端点检查，测试必须失败并恢复。完整构建/CTest，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.coordinator|zzlogg_ui\.update_handoff' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 接通协调者安装启动与普通权限重启" -m "3C 任务五：协调者经凭据文件与受限开关启动安装器，Complete 后复核登记并以原用户身份重启新版，有界启动确认；UAC 取消与早夭均失败关闭，生产工厂仍缺席。"
```

## 任务 6：3B.4 移交收口与协议文档

**文件：** `src/app/kloggapp.h`、`src/app/applicationupdatehandoff.*`、`src/ui/src/updatecheckdialog.cpp`、`tests/ui_acceptance/updatehandofftest.cpp`、`updatecheckuitest.cpp`、`restartcontracttest.cpp`、`docs/development/UPDATE_PROTOCOL.md`。

- [ ] KloggApp 准备丢失时外发通知（信号），ApplicationUpdateHandoff 连接后取消等待中会话；补 CommitExit 在飞期间参与者销毁的测试（残余窗口闭合证据）。
- [ ] options 模态父对话框在交接进行中销毁的测试（对话框随父销毁 → installCancelRequested → 会话取消、窗口恢复）。
- [ ] ApplicationUpdateHandoff 析构改有界等待 + 诊断日志；取消后 reservationHeld 未清空期间的重复 begin 返回可诊断状态而非静默 false。
- [ ] Abandoned 预留与 Blocked 的文案区分（守卫诊断映射 + 三语文案）；评估并记录 Prepared 期 WM_QUERYENDSESSION 语义（结论写入文档，若需代码改动限本任务范围）。
- [ ] UPDATE_PROTOCOL.md：协议 v2/Proceed、bootstrap v3、事务与恢复边界、"准备→预留→握手→提交/取消"时序说明（含观察句柄存续期与引擎 Proceed 前禁止修改）、schema 2 落地清单、3C 未交付项与阶段 4 真实环境验收清单。
- [ ] 裁决并落实 `updaterProtocol` 发布身份字段与线协议 v2 的关系：发布身份语义经 installedrelease 断言锁定为 1，线协议已升 2；明确二者是否应同步，若保持 1 则在文档写明"发布身份协议号与线协议号独立演进"的理由与边界，若升为 2 则同步更新 installedrelease/releaseidentity 测试矩阵。
- [ ] 完整构建/CTest、三语与布局回归，中文提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 收口交接移交项并完善更新协议文档" -m "3C 任务六：准备丢失外发通知消除提交残余窗口，补模态销毁路径与有界析构，区分预留诊断文案，记录协议 v2 与事务恢复边界。"
```

## 验收与交付

- [ ] 每任务独立审查、最终整阶段审查，主控重新构建/测试。
- [ ] master 快进合并、主线 Release 构建与测试，记录程序位置；不推送。
- [ ] 明确本机验收边界（无真实 UAC/HKLM/NSIS 运行），阶段 4 隔离环境验收清单完整移交；不宣称自动更新已上线。

## 执行记录

- 起点 master `3dfbe546`（3B.4 主线验收完成，105/105）；隔离工作树 `.worktrees/update-core`、分支 `codex/installer-update-plan` 继续沿用。用户已授权验证完成后自动快进合并 master，不 push。
- 设计规格 `49adcc5f`；关键裁决：NSIS 脚本不承载事务逻辑（载荷内嵌原生引擎）；协议显式增加 Proceed 消息补齐"应用真实退出"信号（3B 消息集缺口）；UpdateIdentitySchema 升 2 引入落地清单，schema 1 按 Legacy 处理；事务凭据经当前用户私有文件传递而非命令行。
- 任务 1：`4f27556b` 协议 v2（Proceed=8、版本字节 2、ExitConfirmed 状态）与 bootstrap v3（240 wchar 受限数据目录，旧版本/控制字符/相对路径/未终止拒绝）。协调者复制应用进程句柄并以 adopt 防 PID 复用，存活返回 PeerRunning、退出后幂等发 Proceed。真实子进程 TDD 红绿与两组变异证据完整，完整 105/105 通过。独立审查规格符合、质量通过，无关键/重要发现；Proceed 判定句柄持有方改为协调者侧复制被裁定为对简报草图的合理偏离（语义等价、不信任引擎自报）；`updaterProtocol=1` 与线协议号的关系移交任务 6 显式裁决（已补入任务 6）。
- 任务 2：`96ece89d` 持久事务日志与受保护事务目录。`txjournal_win` 二进制日志（32 字节头 + 68 字节定长记录头 + 长度前缀 UTF-16 体，显式小端编码），open 独占创建 `<根>\<txid 十六进制>`（`createExclusiveDirectory` 扩展注入安全描述符，祖先钉住复用 installlock 共享原语），append 先写后刷（flush 可注入观察，生产默认 FlushFileBuffers），replay 严格解析：撕裂/未知操作/未知 flags/错序/事务 ID 不符均 Corrupt 且重放为零操作，Complete 后拒绝追加，重放只读幂等。ACL 主体经 TxJournalOptions 注入（生产 Administrators/SYSTEM 写 + 已验证用户读），测试以当前用户注入并读回真实 DACL 断言（受保护、OICI 继承、读者无法建 journal 的行为证明）。checkVolumeSpace 饱和加法 + 1MiB 日志预留 + 最近现存非重解析祖先查询。TDD 红灯 47 项失败、两组变异均被捕获，完整 106/106 通过。
