# 阶段 4 准备：3C 延后 Minor 甄别与裁决表

来源：主工作区 `.worktrees/update-core/.superpowers/sdd/2026-09-17-install-transaction/progress.md`（2026-09-17，3C 阶段台账）行 28-83 的全部 `minor (deferred)` 条目。

**条数说明**：台账行 84 与本阶段简报均称"25 条"，但 `grep -c "minor (deferred)"` 实际计数为 **28**。本表按实际 28 条逐条裁决，一条不漏；行 74、81 的两条 `minor (cosmetic)`（kloggapp.h 空行）不属于 deferred 范围，不列入。

**核对基准**：本工作树（codex/stage4-release-prep）代码与 master 352ff0c7 一致；表内行号为当前代码行号，括号内为台账引用时的原始行号。裁决分三类：本批修复 / 真实环境验收 / 不修；核对中发现已修复的条目单独标注"已修复"，不计入三类。

| # | 位置 | 问题摘要 | 裁决 | 去向（任务/用例） | 理由 |
| --- | --- | --- | --- | --- | --- |
| 1 | coordinator.cpp:187-189（原 66） | proceedSent 短路先于 failed 检查：abort 后轮询仍见 ProceedSent | 本批修复 | 任务 2 | fail-closed 直觉要求先查 failed；纯代码顺序调整，本机可测 |
| 2 | coordinator.cpp:88-95（原 34-36） | start() adopt 失败早退不调 abort()；担心重试在已持有句柄上再 adopt | 不修 | — | 已核实 adopt 语义：进入即 `handle_.reset(raw)` 并清空 stamp/主体（processidentity_win.cpp:36-44），失败仍接管所有权且空 stamp 过不了任何闸；重试 re-adopt 会先关闭旧句柄，无泄漏无双重持有；早退时 session/channel 尚未创建，无可 abort 之物 |
| 3 | tests/updater/coordinatortest.cpp:65-85（原 60-77） | spawnMappedFixture 以 bInheritHandles=TRUE 继承全部句柄（无 allowlist） | 不修 | — | 测试专用解析驱动，无生产面；原记录已判 acceptable |
| 4 | bootstrap_win.cpp:12-18（原 12） | 控制字符仅查 <0x20（漏 0x7F）；正斜杠盘符被接受 | 不修 | — | 该字段仅为重启提示数据（--data-dir），不承载安装权限；数据面校验足够 |
| 5 | txjournal_win.cpp:202-215（原 202） | append 侧无 replay 强制的总量/条数上限（kMaxJournalBytes/kMaxRecords） | 本批修复 | 任务 2 | 自我 DoS 不对称：写入方可造出 replay 必判 Corrupt 的日志；补对称上限即可，本机可测 |
| 6 | txjournal_win.cpp:225-226（原 226） | replay 以 FILE_SHARE_READ\|FILE_SHARE_WRITE 打开 journal，写共享无必要 | 不修 | — | 任务 2 实证改判：append 进行中并发只读 replay 是既定语义，txenginetest 的 pollJournal kill 点观测机制依赖 FILE_SHARE_WRITE（torn tail 重试即为此设计）；收紧为仅读共享后观测延迟从 +15ms 恶化到事务完成后 +1500ms，中断测试确定性失败。已实证回退，提交 b349b05d |
| 7 | txjournal_win.cpp:171-173,194-196（原 172,194-196） | 基于路径的 Exists 探测与租约后清理存在 TOCTOU 窗口 | 不修 | — | 低危：祖先目录全部钉住（pinAncestors，拒绝删除共享），路径组件不可被替换 |
| 8 | txjournal_win.cpp:122-145（原 128-143） | checkVolumeSpace 只钉最近现存祖先 | 不修 | — | 更深组件尚不存在则无从被换；最近现存祖先已钉住并核验身份，窗口可接受 |
| 9 | txjournal_win.cpp:83-91（原 SDDL 条目） | 事务目录 SDDL 不设 owner；生产环境 owner=Administrators 为隐式行为 | 真实环境验收 | 任务 5 用例 | 需在真实 ProgramData/UAC 环境确认隐式 owner 归属，本机无法断言 |
| 10 | txengine_win.cpp:84-88 | manifest 去重的大小写折叠仅 ASCII（upperKey） | 不修 | — | 输入受控：生产者 GenerateNsisManifest.ps1 用同一 ASCII 折叠规则并在冲突时 throw（脚本 68-70 行）；两侧规则一致，非 ASCII 大小写变体不出现在自发打包文件集 |
| 11 | txengine_win.cpp:359-360（原 undoRecord 条目） | 冲突路径下 Backup 记录 undo 为零操作，孤儿备份字节残留 | 真实环境验收 | 任务 5 用例 | 残留字节在受保护事务目录内、不碍安全，但孤儿备份清理（授权恢复后的事务目录处置）需端到端真实环境验证 |
| 12 | txengine_win.cpp:445-540（prepare） | prepare() 对待改文件不做只读哈希预检，冲突只在 Proceed 后暴露 | 不修 | — | 设计如此且有回滚兜底：冲突触发时自动回滚已应用部分；prepare 保持纯只读是 Proceed 闸门的契约前提 |
| 13 | txengine_main.cpp:23-48 + tests/updater/handofffixture.cpp:88 + tests/updater/coordinatortest.cpp:126 等 | 退出码契约（exitForOutcome/parseTxid/SID 助手）在生产 main、fixture、多处测试中重复 | 本批修复 | 任务 3 | 契约多副本漂移风险；单一来源化（共享头/测试助手），本机可修可测 |
| 14 | txengine_win.cpp:421-423（recheckTarget） | .zzlogg-install-root marker 仅存在性检查 | 不修 | — | 分工如此：引擎侧存在性 + NSIS 侧 VerifyTarget 内容前缀（coordinator restart 侧亦有前缀比对）；marker/manifest 一致性由任务 4 起归 NSIS/引擎各半 |
| 15 | txengine_win.cpp:229-264（parseManifestFile） | manifest 自列表项（.zzlogg-files.manifest 条目）不被解析器拒绝 | 不修 | — | 生产者受控输入；已装 manifest 由引擎自身备份+替换管理（execute 尾部），不接受外部文件清单 |
| 16 | ZzLogg.nsi:265（原任务 4 ACL 条目） | 暂存目录 ACL 仍授 BUILTIN\Users（S-1-5-32-545）读，规格写 Authenticated Users | 真实环境验收 | 任务 5 用例 | 部分已修：51343429 已将受保护根对齐 S-1-5-11（nsi:257）；staging 兄弟目录仍 BU 读。对齐还是裁决接受需在真实环境确认 ACL 实际效果后定 |
| 17 | ZzLogg.nsi:294 | recover 模式仅以 journal.log 存在性为闸，不查受保护树 DACL | 不修 | — | 纵深防御非必需：引擎 recover 会复核受保护镜像 ACL（productionProtectedImage）与注册/marker，NSIS 侧存在性检查只是友好早退 |
| 18 | ZzLogg.nsi:180-181（VerifyTarget） | VerifyTarget 只查 marker/manifest 存在性，内容一致性在引擎侧 | 不修 | — | 分工有意且已有注释（nsi:165-167）；引擎 prepare/recheckTarget 做内容级复核，NSIS 静态层不重复解析 |
| 19 | packaging/windows/GenerateNsisManifest.ps1:82 | 二次 stat 取大小（Get-Item）而非用已打开流的 $fileStream.Length | 本批修复 | 任务 3 | 脚本单行简化，消除 stat 与读流之间的理论窗口；本机可测 |
| 20 | ZzLogg.nsi:271（原 247） | nsis-entry.log 的 FileOpen 缺 IfErrors 分支 | 本批修复 | 任务 3 | 诊断日志非关键路径，但打开失败后 FileWrite/FileClose 行为未定义；补 IfErrors 静默跳过即可 |
| 21 | coordinator.cpp:248-254（原 662-666） | 启动确认循环在管道缺失时无 Sleep 忙等 | 已修复 | — | 51343429 已在循环内加 Sleep(20)（当前 253 行）并附注释；见文末"已核实修复" |
| 22 | coordinator.cpp:212-214（原 628） | restart() 可重入，二次调用会再启一个 GUI 实例 | 已修复 | — | 51343429 已加单次闸：`impl_->restarted.handle()` 非空即 Failed，注释 "Single-shot"；见文末"已核实修复" |
| 23 | coordinator.cpp:53-61（原 484-503） | ShellExecuteEx 成功后身份核验（GetProcessTimes/DuplicateHandle/adopt）失败，提权安装器孤儿运行至自退 | 本批修复 | 任务 2 | 整体 fail-closed（协调器报错、凭据已清），原记录要求注释该窗口；注释级修复，本机完成 |
| 24 | coordinator.cpp:237-239（原 650-653） | restarted.adopt 失败疑似泄漏原始句柄（与 launchCopy 同模式） | 不修 | — | 经核实不构成泄漏：adopt 进入即 `handle_.reset(raw)` 接管所有权（processidentity_win.cpp:37），失败时局部 restarted 析构即关闭句柄；launchCopy 亦同（失败路径 adopt 进 child 交收割者） |
| 25 | coordinator.cpp:246 | 单实例端点名依赖硬编码 L"ZzLogg.exe" 与 GUI fileName() 大小写一致 | 真实环境验收 | VM 手册注意项 | 改名场景 fail-closed（重启确认失败而非误放行）；原记录即 "note for stage 4"，写入验收注意项即可 |
| 26 | tests/updater/coordinatortest.cpp:490,574,598 | 提权 harness 跳过 restart/confirm/recheck 三处断言（仅 cout 说明） | 真实环境验收 | 任务 5 用例 | CI 提权账户覆盖缺口；需在真实提权环境补齐三处断言或登记 VM 手册手动项 |
| 27 | src/app/applicationupdatehandoff.cpp:33 + tests/ui_acceptance/updatehandofftest.cpp:890-914 | 析构等待测试固定 ~15s 运行时，kDestructWaitMs 不可注入 | 本批修复 | 任务 2 | 测试 seam：把 kDestructWaitMs 改为可注入参数，缩短 CI 时间；本机可修可测 |
| 28 | tests/ui_acceptance/updatehandofftest.cpp:209,899-906 | warningCapture 为裸全局指针，qScopeGuard 恢复更整洁 | 不修 | — | 测试专用、GUI 单线程、QVERIFY 失败即中止该用例；整洁性事项，风险已接受 |

## 裁决统计

- 本批修复：7 条（#1、#5、#13、#19、#20、#23、#27）→ 任务 2（#1、#5、#23、#27）、任务 3（#13、#19、#20）
- 真实环境验收：5 条（#9、#11、#16、#25、#26）→ 任务 5 用例 / VM 手册
- 不修：14 条（#2、#3、#4、#6、#7、#8、#10、#12、#14、#15、#17、#18、#24、#28），接受理由见表（#6 为任务 2 实证后改判，见表内理由与提交 b349b05d）
- 已修复（不计入裁决）：2 条（#21、#22）

## 已核实修复

| # | 条目 | 证据 |
| --- | --- | --- |
| 21 | 启动确认忙等 | 提交 `51343429`（"fix: 修正 NSIS 事务目录冲突与启动确认忙等"）在 coordinator.cpp restart 确认循环内加入 `Sleep(20)`；当前代码 248-254 行已含 Sleep 与 "never busy-poll" 注释。`git show 51343429 -- src/updater/coordinator.cpp` 可复核 |
| 22 | restart() 重入 | 同一提交在 restart() 入口加入 `impl_->restarted.handle()` 单次闸与 "Single-shot" 注释；当前代码 212-214 行 |

另：#16 涉及的受保护根 ACL 亦由 `51343429` 部分修复（根目录创建时加固为 Administrators/SYSTEM 完全 + Authenticated Users 只读，nsi:257）；staging 兄弟目录的 BUILTIN\Users 读授权（nsi:265）不在该次修复范围，故 #16 仍列入真实环境验收。
