# 独立更新进程与受限交接（3B.3）实现计划

> **面向 AI 代理的工作者：** 使用 subagent-driven-development 逐任务实现；步骤使用复选框跟踪。

**目标：** 构建不依赖安装目录 DLL 的独立更新协调者，用真实测试子进程验收安全启动、受限握手及退出条件；生产执行保持关闭。

**架构：** 共用源文件分别构建应用 /MD 与协调者 /MT 依赖闭包。小型协议状态机与 Windows 本地进程通道分离；协调者从事务专属运行副本运行。测试安装器是单独测试目标，绝不通过正式程序的参数启用模拟成功。

**技术栈：** C++17、Windows API、CMake、无 Qt 的原生测试，现有更新核心。

**规格：** `docs/superpowers/specs/2026-09-16-installer-handoff-design.md` 第 4、6 节；继承 `2026-09-15-installer-only-update-design.md` 的权限边界。

## 全局约束

- 仅 Windows x64；协调者和其全部 C/C++ 依赖使用静态 CRT，不依赖 Qt 或安装目录第三方 DLL；应用已有 /MD 不变。
- 不调用真实 NSIS、不弹 UAC、不写 HKLM/信任库/真实安装目录、不修改应用 UI 或会话退出路径。生产证书策略为空、生产入口关闭，不提供参数绕过。
- 测试替身只编译到测试目标，不部署；其成功不是生产签名或管理员事务验收。
- 本地访问控制、随机单次令牌、事务 ID、持续持有的真实进程句柄及创建时间一起验证；拒绝远程、重放、超长/截断、错序和错误进程。令牌不进命令行或普通日志。
- 启动 API 成功不能授权应用退出；只有匹配安装器的等待应用退出消息才能进入可提交退出状态。取消/拒绝/超时/对端退出均失败关闭。
- 安装目录锁按稳定目录身份区分，不按程序名或可伪造路径字符串；覆盖当前用户与其他用户竞争，无法协调时阻塞，不强杀。
- 主程序所有启动入口、多窗口保存和 UI 接线由 3B.4 消费本阶段接口；本阶段不宣称现有 GUI 已接入锁。真实管理员安装及重启由 3C 实现。
- 在 `.worktrees/update-core` 工作，保留根工作区用户文件。中文标题加中文详细正文提交；审查及验证完成自动 `--ff-only` 合并 master，不自动 push。

## 文件结构

- `src/update/CMakeLists.txt`、`cmake/ZzUpdateDependencies.cmake`：同源双 CRT 目标，不复制实现代码。
- `src/updater/CMakeLists.txt`：Windows 原生协调者及通道库目标；从顶层挂接。
- `src/updater/handoffprotocol.{h,cpp}`：固定有界报文和退出前状态机。
- `src/updater/installlock_win.{h,cpp}`：稳定目录租约与按目录身份的跨进程锁。
- `src/updater/processidentity_win.{h,cpp}`：真实句柄、创建时间、用户/登录会话/提升状态检查。
- `src/updater/localchannel_win.{h,cpp}`：受限命名管道、有界超时 I/O 与身份验证。
- `src/updater/runtimecopy_win.{h,cpp}`：事务专属目录、稳定源/目标租约、按句柄复制校验、安全创建进程。
- `src/updater/coordinator.{h,cpp}`、`main_win.cpp`：组合生命周期及关闭的生产入口。
- `tests/updater/`：原生测试与独立测试进程；按上述职责分文件，避免超大测试或实现文件。
- `docs/development/UPDATE_PROTOCOL.md`：真实验收范围、后续接线要求。

## 任务 1：静态依赖闭包、有界协议与安装目录锁

**文件：** 修改依赖 CMake、核心 CMake、顶层 CMake；创建 updater CMake、协议和锁文件，以及 `tests/updater/CMakeLists.txt`、`protocoltest.cpp`、`installlocktest.cpp`、`dependencytest.cmake`。

- [x] 先编写协议/锁失败测试，记录 RED；定义接口后执行运行失败测试，不能仅以编译缺符号代替行为 RED。

```cpp
enum class MessageKind : uint32_t { Hello=1, Ready=2, AwaitingAppExit=3,
    CommitExit=4, Cancel=5, Failed=6, Complete=7 };
using TransactionId = std::array<uint8_t,16>;
using SessionToken = std::array<uint8_t,32>;
struct Message { MessageKind kind; TransactionId transaction; SessionToken token; };
enum class HandoffState { Connecting, Ready, AwaitingAppExit, ExitCommitted, Complete, Aborted };
// Explicit byte encoding; exact fixed size, magic/version, no native struct serialization.
// Reject bad magic/version/kind, missing/trailing bytes, zero transaction/token.
// State starts Connecting: Hello -> Ready; AwaitingAppExit -> AwaitingAppExit;
// CommitExit -> ExitCommitted; Complete -> Complete. Cancel/Failed -> Aborted.
// Wrong sequence/token/transaction or terminal replay aborts; Ready is coordinator reply,
// not an input transition. canExit() is true only at AwaitingAppExit/ExitCommitted.
```

- [x] 编写参数表测试：正确顺序、仅启动/Hello 不可退出、提前 Complete/Commit、重复握手、其他事务/令牌、全零字段、全部终态重放、截断与超长；取消从每个未完成阶段不可退出。固定协议消息只承载控制信息，安装选择记录不经此消息变为授权。
- [x] 同源定义 core/execution/monocypher 两套目标；辅助函数明确 CRT 属性，应用目标保持不变，协调者目标全部 `/MT`（Debug `/MTd`）。不要用全局 CRT 开关，不改变发布身份生成语义；新库/测试不链接 Qt。头文件 json 复用。
- [x] 锁接口使用 RAII move-only，获得目录真实 handle，拒绝 reparse/网络/无效根、保留祖先稳定性，标识包含卷序号和文件 ID。可使用 Global 命名 mutex：名称是目录身份哈希、显式 DACL 允许经过身份验证的本地用户 SYNCHRONIZE/MUTEX_MODIFY_STATE、拒绝访问/预占/现存对象返回废弃状态时均安全阻塞；不得把 WAIT_ABANDONED 当作成功。更新独占租约和新入口探测均基于同一对象。锁不是提升权限凭据。最后句柄关闭后内核对象消失，此锁不提供持久崩溃恢复判定；该责任属于 3C 受保护事务日志，禁止为弥补此边界在安装目录写普通用户标记。
- [x] 真实子进程或本地独立实例测试同目录竞争失败、释放后成功、其他目录互不影响、路径大小写别名一致、路径替换拒绝/持有时失败；记录平台限制，不伪称其他用户实测。
- [x] 最少实现通过测试，再移除一个错序/令牌守卫做变异测试并恢复。目标需实际编译链接 core/execution 静态闭包，不能只检查未使用库属性。
- [x] 完整 Release 构建及 CTest，提交任务 1。

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 建立独立更新器静态核心与交接协议" -m "3B.3 任务一：隔离静态 CRT 依赖，增加严格报文、退出状态机和按安装目录身份协调的锁。"
```

## 任务 2：安全运行副本、真实进程通道与协调者验收

**文件：** 创建进程身份、本地通道、运行副本、协调者及 main 文件；扩展 updater CMake、tests/updater（runtimecopytest、channeltest、coordinatortest、独立 fixture）；更新协议文档。

- [x] 先为生产启动门禁、复制文件稳定性、真实管道握手失败和成功写运行 RED；测试 fixture 仅在测试目录目标中定义，正式程序不编译测试入口。

```cpp
// Ownership outline, exact concrete names follow task 1 headers.
// ProcessIdentity owns a real process HANDLE opened with QUERY_LIMITED_INFORMATION|SYNCHRONIZE;
// capture GetProcessTimes creation time, compare live PID, user/logon SID and session.
// LocalChannel owns one FIRST_PIPE_INSTANCE, REJECT_REMOTE_CLIENTS, overlapped message pipe;
// DACL for the exact logon SID. Client connects with SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION.
// Verify GetNamedPipeClientProcessId/GetNamedPipeServerProcessId against held expected handles.
// Every connect/read/write has one absolute deadline; CancelIoEx + drain before storage release.
// RuntimeCopy owns source/destination stable leases and a random transaction-only directory;
// copy from held source handle, flush, verify byte count/SHA256, verify identity before CreateProcessW.
// CreateProcessW uses explicit absolute application name, known working directory,
// STARTUPINFOEX handle allowlist and CREATE_NO_WINDOW. Secrets travel via inherited bootstrap
// handle, not arguments. Bootstrap binds parent handle/PID/creation time, endpoint and transaction.
// Coordinator owns process handles and leases through final process exit (not window closure).
```

- [x] 随机数来自 BCryptGenRandom，失败拒绝。管道名只用本地固定前缀及随机事务 ID，名称非认证凭据；严格上限、超时、错身份即关连接不无限重试。令牌只经受限继承 bootstrap 传送，限制继承句柄到该子进程；对端需同时符合真实身份与消息凭据。测试 PID 不匹配、创建时间不匹配、提前退出、截断/超长、重放、错误顺序、超时以及正确链路。
- [x] 运行副本目录使用 CREATE_NEW 等价独占创建及限制 DACL，拒绝所有祖先重解析点；目标文件 CREATE_NEW、不可重用其他事务残留。源与目标字节一致仅说明同源，不作为管理员信任根。源验证后释放源租约，让原安装文件可被替换；目标租约保持到真实进程结束。证明子进程执行路径确在新目录，运行时原文件能重命名/替换，副本不可修改、删除、替换且祖先不可重定向；清理只删除持有且身份一致的精确文件/空目录，不递归清理用户路径。失败保留可诊断残留优于宽泛删除。
- [x] 独立生产 `ZzLoggUpdate.exe` 完成 bootstrap 身份与协议处理后返回明确 ExecutionDisabled，不接受任意安装器路径或命令参数执行。正常入口不得用任意无签名 EXE 充当安装器。把通道/协议组合成可测试协调接口：fixture 通过独立测试链接驱动该接口，不通过生产 exe 的开关伪装成正式成功。
- [x] 协调者依赖注入仅保留在内部算法/测试组合，不新增可伪造 VerifiedPackage 的公有构造。生产安装启动能力仍受 3B.2 校验及缺发布者策略门禁；3B.3 不增加真正 ShellExecute/runas。真实测试安装器 Hello 后保持存活，只有其已认证 AwaitingAppExit 才通知调用方可提交退出；启动成功、普通 Ready、错进程发来的 AwaitingAppExit 均不可。
- [x] 测试子进程链覆盖成功、取消、启动失败、握手拒绝/超时、父进程和安装器提前退出、退出消息后进程仍存活（不能提前宣称结束）。已提权父进程禁止自动重启，返回手动启动要求；本阶段不真实启动新版 GUI。不能因测试超时按名称终止其他进程，必要清理仅限本测试拥有的句柄。
- [x] 静态 CRT/依赖验收从实际 exe PE imports（包含 delay imports）检查，不依赖 PATH 中的 Qt/MSVC DLL；复制到隔离目录运行生产关闭门禁测试，确认无 Qt DLL / vcruntime DLL / msvcp DLL 依赖。证明执行核心/加密链接目标整个闭包 CRT 一致。ARM64/ARM64EC 不产生支持假象。
- [x] 至少对退出前握手守卫和进程身份校验做变异测试、恢复后聚焦通过。完整 Release 构建和 CTest、更新协议和测试边界，中文提交任务 2。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_updater\.' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 实现独立更新进程的受限安全交接" -m "3B.3 任务二：验证事务运行副本、真实进程身份和本地握手，生产安装保持关闭，测试替身不部署。"
```

## 任务 3：消除全套验收中的重定向期限测试竞争

**文件：** `tests/updateqt/manifestfetchertest.cpp`；仅在需要受控回复时修改测试工具 `tests/updateqt/scriptednetwork.h`。不改生产下载实现和超时策略。

- [x] 保存并分析主控失败证据 `out/ui-vs/3b3-main-verification-fetcher.txt`：redirectDoesNotResetTotalDeadline 预期两次请求但实际一次，测试在重定向发生前已超时。现有脚本通过九个串行 10ms singleShot 加事件循环等待来完成首个响应，而总期限仅 150ms；调度累计漂移可能耗尽总期限。这不构成重置期限实现缺陷证据。
- [x] 将测试调度改为受控响应或少量有清晰间隔的事件，使首响应在总期限之前有充分余量完成，第二响应在原总期限之后、错误重置期限之前完成；保持 `Timeout`、无成功、两次请求断言，不降低覆盖、不靠重试掩盖失败。

```cpp
// Behaviour to preserve, regardless of fixture scheduling details:
QCOMPARE(qvariant_cast<FetchError>(failure[0][0]), FetchError::Timeout);
QCOMPARE(success.count(), 0);
QCOMPARE(network->requests.size(), 2);
// Temporarily restart total_ when handling a valid redirect: test MUST fail;
// restore production source and rerun. No production change in the final diff.
```

- [x] 记录新测试针对“重定向重启 total_”变异确实失败的证据并恢复；聚焦测试多次复验，所有失败先保存 Qt 输出，不能只保留 CTest 壳日志。测试时间关系须能区分真实总期限与重置期限，不只是把原有几个毫秒值调大。
- [x] 独立小范围审查后提交中文测试修复；最终整阶段全套负责集成验证。

## 验收与交付

- [x] 每个任务独立审查；最终整阶段审查及主控独立构建/测试。
- [x] 自动快进合并 master，在主线重新编译和测试；保留工作树证据，不 push。
- [x] 明确仅完成 3B.3 的受限基础，不声称真实更新已可用；下一步 3B.4 保存/取消/退出与新入口锁接线。

## 执行记录

- 起点 `d8d504e5`，master 与隔离分支相同；上一阶段主线 Release 构建成功，90/90 CTest 通过（100.53 秒）。用户已指定后续验证完成直接合并 master。
- 本阶段隔离工作区基线 90/90 通过（101.49 秒）。任务 1 提交 `9cfac53f`，独立审查规格与质量通过，无 Critical/Important/Minor；完整 Release 构建与 94/94 测试通过（101.14 秒），Debug 聚焦 4/4 通过。日志前缀 `out/ui-vs/3b3-task1-`。
- 协议空桩实际运行触发 93 条失败断言，锁空桩实际运行失败；移除令牌守卫触发 4 条失败断言，移除网络拒绝 ACE 触发真实对象 ACL 断言失败，恢复后全部通过。不是仅检查源文件文本。
- 任务 1 裁决：现存 mutex 的 WAIT_ABANDONED 必须拒绝，但最后句柄消失后没有持久状态；不向安装目录写用户标记，持久恢复归 3C 受保护日志。若该职责划分不足，须在开放实际写入前调整恢复协议。
- 目录锁测试发现属性访问本身不足以阻止空目录重命名，已加入 FILE_LIST_DIRECTORY 并由真实目录/祖先替换测试验证。既有 StablePackage 同时持有叶文件且有祖先替换测试，未据此推断旧验证失效。
- 主控核对生成项目：更新器库为 MultiThreaded / MultiThreadedDebug，应用核心仍为 MultiThreadedDLL / MultiThreadedDebugDLL。跨用户与不同提权身份的真实运行尚未验收；真实 DACL 检查不替代该环境矩阵。
- 任务 2 实现 `fa447e37`：运行副本、受限 bootstrap、真实进程身份/本地通道、非阻塞取消清理和关闭的生产入口已完成。最终原子目录版本完整构建与 99/99 测试通过（106.91 秒）。生产 EXE 实际普通依赖仅 ADVAPI32.dll、KERNEL32.dll，普通/延迟导入及静态库指令检查通过；不部署 fixture、不修改应用 UI。
- 任务 2 裁决：把独占建目录后另行打开改为系统 NtCreateFile 原子取得目录句柄，避免同登录用户替换窗口；API 不可用时拒绝操作，不回退到有窗口的路径。若兼容性假设不成立，该环境的更新交接保持不可用，须先修正并验收。
- 任务 2 审查发现忙管道预算变零后 WaitNamedPipeW 会采用服务器默认等待，`9c3a41b8` 已修复。真实忙管道默认等待 500ms 的回归先失败后通过；channel/coordinator 聚焦 2/2 通过（6.07 秒），定向复审无剩余或新增问题。
- 主控对 `fa447e37` 独立构建成功，新更新器测试全部通过，但全套为 98/99（101.99 秒）；已保存 Qt 详细输出，确认旧 fetcher 重定向期限测试在请求数 1/2 处失败。任务 3 专门修复该测试的短定时器累计调度竞争，生产下载行为不变；不能把此前偶发通过说成已修复。
- 任务 3 提交 `a1d03fe1`，仅修改测试调度；独立复现旧失败、重启总期限变异失败，恢复后聚焦 10/10、提交后 3/3 通过，独立审查无关键或重要问题。裁决是修测试调度而非改生产超时；若仍复发须重新调查，不能降低断言或以重试掩盖。
- 辅助直接运行 fetcher 曾缺少 Qt TLS 插件搜索路径而出现警告，审查记为次要证据问题；主控使用项目正式 CTest 环境复验，`out/ui-vs/3b3-final-fetcher.txt` 为 36/36 且无 QWARN。最终 `a1d03fe1` 独立 Release 构建退出 0、整仓 CTest 99/99 通过（102.91 秒），日志为 `out/ui-vs/3b3-final-build.log` 和 `out/ui-vs/3b3-final-tests.log`。

## 主线交付记录

- 最终整阶段审查完整覆盖 `d8d504e5..b566c215`，无 Critical、Important 或剩余 Minor。主线已从 `d8d504e5` 快进到 `b566c215`，未创建分叉合并提交，未推送。
- 主线 Release 构建退出 0；CMake 配置仍有第三方弃用/策略警告，未阻断构建。完整 CTest **99/99 通过（106.45 秒）**，fetcher **36/36**；证据位于主仓库 `out/ui-vs/3b3-master-merge-build.log`、`3b3-master-merge-tests.log`、`3b3-master-merge-fetcher.txt`。
- 当前计划的临时审查证据归档至隔离工作树 `out/ui-vs/updater-handoff-evidence`，含子模块的工作树与分支保留，不强制清理。用户既有 `.arts/`、`serach.png`、`serach.svg` 未修改。
- 仅交付独立更新器受限基础，生产安装执行仍关闭。下一阶段为 **3B.4**：应用保存/取消/退出流程、启动入口锁接线及更新器部署；真实安装、提权和持久恢复仍需后续验收。

## API 参考

- [命名管道访问控制与登录 SID](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)
- [显式继承句柄列表](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
