# 阶段 4 VM 真实环境验收手册（Windows 安装版在线更新）

本手册覆盖 CI 无法自动化的真实环境验收条目。验收脚本包
`tools/acceptance/`（运行器 `Invoke-UpdateAcceptance.ps1`）中用例 01–07
已由 CI 冒烟流水线（`.github/workflows/update-smoke.yml`）自动化；用例
08–12 为固定 SKIP 手工项，其 SKIP Evidence 逐字指向本手册 §2.1–§2.5。

**全局纪律：**

- 全部条目在隔离 VM 中执行，绝不在开发主机或生产机器上跑真实
  UAC/HKLM/ProgramData 写入。
- 每个条目执行前回到对应快照基线；执行后填写该条的记录栏
  （日期 / 执行人 / VM 镜像与快照名 / 结果 PASS·FAIL·BLOCKED /
  证据路径（截图、日志、报告文件）/ 备注）。
- 引擎/安装器退出码契约（`src/updater/txcontract_win_p.h` 单一来源）：
  0 = Applied/Recovered/NothingToRecover；2 = UsageRejected（受限入口
  用法/参数拒绝）；40 = ExecutionDisabled、41 = BootstrapRejected
  （bootstrap 阶段）；42 = Rejected；43 = Conflict；44 = RolledBack；
  45 = NeedsAuthorizedRecovery；46 = RecoveryFailed；48 =
  InstallerRuntimeFailure（安装器侧运行期失败；引擎绝不以此码退出，
  协调者对任何非零安装器退出均判 Failed）。协调者侧 UAC 用户拒绝映射为
  Cancelled（ERROR_CANCELLED）。
- 预期结果引用的清单条目原文出自
  [UPDATE_PROTOCOL.md](UPDATE_PROTOCOL.md)「3C 未交付项与阶段 4 真实环境
  验收清单」及相邻 3C/3B 章节；引文逐字摘录，验收时以原文为准。

---

## §1 环境获取与快照基线

### §1.1 环境获取

任选其一：

1. **微软官方免费 Windows 11 评估 VM**（推荐）：从
   <https://developer.microsoft.com/windows/downloads/virtual-machines>
   下载，提供 Hyper-V / VirtualBox / VMware / Parallels 镜像，开箱即用，
   含评估期 Windows 11 Enterprise。
2. **90 天 Windows 11 Enterprise 评估 ISO**：从微软评估中心下载 ISO，
   自建 Hyper-V / VirtualBox / VMware 虚拟机安装。

VM 配置要求：

- 至少双固定卷（C: + D:）：跨卷暂存预检（§2.1、§3.2、§3.6）依赖暂存卷
  （%ProgramData% 所在卷）与安装目标/备份卷分离；评估镜像默认单卷，需
  自行附加第二块虚拟磁盘并格式化 NTFS。
- 两个本地管理员账户（§2.3 用）：安装后新建第二个本地管理员并各完成
  一次交互登录。
- UAC 保持默认开启，不做任何降级（§2.2 需要真实 consent 界面）。
- 安装 Windows SDK（含 signtool，§2.6 需要）；签名闭环只能在交互桌面
  完成——本机/无人值守环境无法确认 Root 导入的 GUI 安全警告。
- 可选：RAM 盘工具（如 ImDisk）或小容量 VHD（§3.2 空间不足注入用）。

### §1.2 快照基线建议

| 快照 | 内容 | 用途 |
| --- | --- | --- |
| `baseline-clean` | 全新系统 + 上述配置完成，未装 ZzLogg | 全部分支的起点 |
| `baseline-old-installed` | 已装旧版 ZzLogg，含真实使用产生的用户数据（%APPDATA%\ZzLogg） | §2.1 / §2.5 / §3.1 / §3.4 |
| `baseline-new-installed` | 已装当前新版 | §2.4 与独立复核 |
| 每条目执行前 | 回到对应基线再逐项执行 | 保证条目间无污染 |

验收脚本包在 VM 内也可复跑：`Invoke-UpdateAcceptance.ps1 -SetupExe <包>
-InstallDir <目录> -ReportPath <报告>`，用例 01–07 在管理员 PowerShell
中真实执行，08–12 留 SKIP 痕指向本手册对应小节。

---

## §2 用例 08–12 手工验收（脚本固定 SKIP 项）

### §2.1 用例 08：旧版升级端到端（真实协调者链）

对应脚本 `tools/acceptance/cases/08-upgrade-endtoend.ps1`（固定 SKIP：
"需协调者生产链路真实凭据 + 旧版应用，CI 无法构造活协调者身份"）。

**前置：**

- 快照 `baseline-old-installed`（旧版已装，登记 `UpdateIdentitySchema`
  缺失或 = 1，即 Legacy/旧登记形态）。
- 新版安装包（新安装写入 `UpdateIdentitySchema=2`）与配套升级事务载荷。
- 驱动真实协调者链路的手段：应用内"退出并更新"生产可见性仍关闭（生产
  会话工厂缺席，见 UPDATE_PROTOCOL.md「仍未交付」清单），本条以提权
  harness / 专用测试目标驱动协调者生产链路（真实预约 → 运行副本 →
  凭据文件 → ShellExecuteEx runas 安装器 → 引擎事务 → Proceed → Complete
  → 复核 → 重启）；记录栏必须注明驱动方式与构建配置。
- 双卷环境（暂存 %ProgramData% 在 C:、安装目标可放 D:），同时覆盖真实
  跨卷暂存预检——CI 用例 07 断言偏弱（普通安装不跑引擎空间预检），
  **真实跨卷暂存预检在本条链路覆盖**（见 §3.6）。

**逐步操作：**

1. 确认旧版登记形态：`reg query "HKLM\SOFTWARE\Microsoft\Windows
   \CurrentVersion\Uninstall\ZzLogg"`，记录 UpdateIdentitySchema 现状。
2. 驱动协调者生产链路发起升级；在真实 UAC consent 界面点"是"（拒绝
   路径见 §2.2）。
3. 观察受限入口 `/ZzLoggUpgrade=<16 位小写 hex 定位名>` 启动引擎
   （五组 flag/value：--install/--staging/--txroot/--txid/--version），
   引擎按日志先写后操作执行替换事务。
4. 事务完成后协调者复核登记/标记/清单，以原用户身份重启应用；已提权
   宿主应返回 ManualRestartRequired（不自动重启）。
5. 断言（逐项记录）：
   - 安装目录文件已替换为新版；旧版残留按清单清理。
   - HKLM 卸载项 `DisplayVersion` 更新、`UpdateIdentitySchema=2`
     （DWORD）。
   - `.zzlogg-install-root` 标记与 `.zzlogg-files.manifest`（magic
     `ZZTXMAN1` + u32le version=1）在位。
   - 受保护根 `%ProgramData%\ZzLogg\UpdateTransactions` ACL：
     `icacls` 复核——属主 Administrators/SYSTEM，Administrators/SYSTEM
     完全 + Authenticated Users 只读；兄弟暂存目录
     `staging-<定位名>` 为 Administrators/SYSTEM 完全 + Users 只读。
   - journal 事务目录 `<根>\<定位名>` 只归引擎所有；Complete 后按契约
     处置（保留供授权恢复或清理，记录实际状态）。
   - 引擎/安装器退出码符合契约（成功 0；失败见全局纪律码表）。
   - 跨卷形态下暂存卷与备份/目标卷分别空间预检生效，无误报无误拒。
6. 补充断言（裁决表 UPDATE_STAGE4_TRIAGE.md 真实环境验收项）：
   - **#9**：真实 ProgramData/UAC 环境下事务目录属主归属确认（SDDL
     不显式设 owner，生产隐式 owner=Administrators 的实际落地）。
   - **#11**：授权恢复后孤儿备份字节的端到端处置确认。
   - **#16**：staging 兄弟目录 ACL 主体 BUILTIN\Users 与规格
     Authenticated Users 差异的真实效果确认（对齐或裁决接受）。
   - **#26**：提权 harness 中 restart / confirm / recheck 三处在 CI
     被跳过的断言，在真实提权环境补齐或登记为本条手工观察项。

**预期结果（清单原文）：**

> - 真实 NSIS 安装/升级/恢复包端到端（VerifyTarget、禁 /D=、退出码传播、静默页面行为）；
> - 真实 HKLM 写入（卸载项、UpdateIdentitySchema=2）与登记回滚恢复；
> - 真实 ProgramData ACL 树下引擎受保护根 accept 路径（upgrade 的 prepare 与 recover 双模式，含根已存在/已加固的幂等进入）；
> - 分卷（暂存与备份不同卷）空间预检端到端（本机单固定盘环境受限）；
> - NtCreateFile 原子目录创建与目录租约在目标 Windows 版本上的兼容性；

另对照协议原文："Complete 后协调者复核登记、标记、清单一致，再以原
用户身份重启应用；确认失败保留事务备份供授权恢复。"

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 驱动方式 | 退出码 | ACL 复核 | 跨卷预检 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §2.2 用例 09：真实 UAC 提权与用户拒绝路径

对应脚本 `tools/acceptance/cases/09-uac-cancel.ps1`（固定 SKIP：
"CI 无交互桌面，无法弹真实 UAC 并走用户取消路径"）。

**前置：** 交互桌面 VM，UAC 默认开启；§2.1 链路已可驱动。

**逐步操作：**

1. 触发升级链，UAC consent 出现时点"是"：确认 ShellExecuteEx runas
   成功、安装器以提权身份运行、协调者身份核验（PID/创建时间/令牌）
   通过。
2. 回到基线，再次触发，UAC consent 点"否"（或 ESC）：观察
   ERROR_CANCELLED → Cancelled 映射——协调者报告 Cancelled，不产生
   staging/凭据残留（当前用户私有临时目录下凭据文件已清理），应用
   不被退出、不产生任何 HKLM/安装目录写入。
3. 记录 consent 界面显示的发布者信息与二进制路径（截图）。

**预期结果（清单原文）：**

> - 真实 UAC 提权链路（含用户拒绝路径）与跨完整性级别令牌/身份验证可行性；

另对照："安装器经可注入 seam 以 ShellExecuteEx runas 启动……
ERROR_CANCELLED 映射为 Cancelled（UAC 拒绝）。"

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 提权成功观察 | 拒绝路径观察 | 残留检查 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

### §2.3 用例 10：另一管理员账号下的升级

对应脚本 `tools/acceptance/cases/10-multi-admin-account.ps1`（固定
SKIP："需第二个管理员账户的交互会话，CI 单账户无人值守环境无法构造"）。

**前置：** VM 已建第二个本地管理员账户并完成过一次交互登录；§2.1
链路已可驱动。

**逐步操作：**

1. 账户 A 安装并准备升级事务（或回到 `baseline-old-installed` 后以 A
   登录准备）。
2. 切换/注销到账户 B（交互会话），由 B 触发升级/恢复链路，UAC 以 B
   凭据确认。
3. 观察跨完整性级别令牌与身份验证：协调者对安装器进程的身份核验
   （真实进程句柄、创建时间、用户 SID、登录 SID、会话）在跨账户场景
   下通过或按契约失败关闭。
4. 复核 ACL 主体行为：凭据文件（当前用户 DACL）、事务目录/受保护根在
   B 账户下的授权形态；A 创建的事务由 B 恢复时须走授权路径。
5. 注意项（裁决表 #25）：单实例端点名依赖硬编码 `L"ZzLogg.exe"` 与
   GUI fileName() 大小写一致；改名场景应 fail-closed（重启确认失败
   而非误放行）——记录实际观察。

**预期结果（清单原文）：**

> - 真实 UAC 提权链路（含用户拒绝路径）与跨完整性级别令牌/身份验证可行性；

另对照："双方核验实际端点 PID、持续持有的真实进程句柄、创建时间、
用户 SID、登录 SID 和会话。"任何身份不符必须失败关闭，不得放行。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 账户 A/B | 身份核验观察 | ACL 主体观察 | #25 注意项 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §2.4 用例 11：关机/注销与 Prepared 窗口（WM_QUERYENDSESSION）

对应脚本 `tools/acceptance/cases/11-session-end-queryendsession.ps1`
（固定 SKIP："需真实关机/注销交互（Prepared 期 WM_QUERYENDSESSION 否决
抽查），CI 无法触发真实会话结束"）。

**前置：** 可进入 Prepared 状态的构建/驱动方式（交接到达 Waiting 前，
窗口冻结快照变更）；快照 `baseline-new-installed`。

**逐步操作：**

1. 使应用进入 Prepared（保存全部窗口并同步会话成功，窗口保留但冻结；
   交接在 Waiting 前的窗口期）。
2. 窗口期内发起系统注销或关机。
3. 观察：系统呈现"应用阻止关机"界面（应用对 WM_QUERYENDSESSION 投
   否决票）；选择"取消"回到桌面，再选择"强制关机/注销"各观察一次。
4. 验证窗口期结束后行为：交接提交退出（进程真实退出）或取消恢复后，
   再次关机/注销不再被否决。
5. 记录否决窗口的实际时长量级（应为秒级）。

**预期结果（清单原文）：**

> - 关机/注销（WM_QUERYENDSESSION）与 Prepared 窗口的真实交互抽查；

另对照评估结论："**有界且可接受，不引入代码改动**。Prepared 窗口期被
设计为秒级……否决只发生在该窗口内，随后要么提交退出（进程真实退出，
关机继续），要么取消恢复（正常响应后续关机请求）。"若观察到窗口期外
仍否决、或取消恢复后仍阻塞关机，判 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 否决界面截图 | 窗口时长 | 强制继续观察 | 恢复后关机 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §2.5 用例 12：文件占用升级失败与回滚

对应脚本 `tools/acceptance/cases/12-file-in-use-upgrade.ps1`（固定
SKIP："依赖用例 08 的真实升级链（升级事务中目标文件被占用场景），
CI 无法构造活协调者会话"）。

**前置：** §2.1 链路已可驱动；快照 `baseline-old-installed`。

**逐步操作：**

1. 记录安装目录全部文件 SHA-256 基线（含 ZzLogg.exe）。
2. 构造真实升级事务；在引擎替换阶段保持目标文件被占用——例如另起
   一个 ZzLogg.exe 实例运行中，或以程序方式对 ZzLogg.exe 持无删除
   共享的读句柄。
3. 观察引擎在冲突点的行为：应触发 Conflict（43）或对已应用部分逆序
   回滚后 RolledBack（44），不得报告 Applied。
4. 断言回滚完整性：安装目录逐文件哈希与基线一致；登记未推进
   （DisplayVersion/UpdateIdentitySchema 保持旧值）；事务现场按契约
   保留或清理。
5. 解除占用后按授权路径恢复/重试，确认可正常完成（衔接 §3.1）。

**预期结果（清单原文）：**

> - 真实安装目录文件替换事务与中断恢复（杀进程/断电注入点）；

另对照："普通失败逆序回滚；中断（杀进程/断电点）后经授权路径幂等
恢复；……Corrupt 或 0 字节日志呈现为 NeedsAuthorizedRecovery（保留
现场，交授权恢复，绝不自动继续）。"占用冲突下出现部分替换未回滚、
登记与文件不一致、或误报成功，均判 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 占用方式 | 退出码 | 回滚哈希比对 | 登记状态 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §2.6 签名闭环交互验收（测试签名工具链）

控制者裁决新增条目。工具链：`packaging/windows/
New-ZzLoggTestCertificate.ps1` 与 `Sign-ZzLoggArtifacts.ps1`。**本条
只能在交互桌面完成**：本机无 signtool，且 `-TrustCurrentUser` 的 Root
导入会弹 Windows 根证书安全警告，GUI 确认无法无头完成。

**前置：** 交互桌面 VM；已装 Windows SDK（signtool 可经 PATH →
vswhere → Windows Kits 10 bin 定位）；待签名的构建产物（.exe/.dll）。
红线：测试证书仅测试用途（Subject 含 `Test` 与 `NOT FOR PRODUCTION`）；
PFX/私钥绝不入库；证书与信任只写 `Cert:\CurrentUser\*`，绝不写
LocalMachine。

**逐步操作：**

1. `New-ZzLoggTestCertificate.ps1 -TrustCurrentUser -ExportPfx
   <临时路径.pfx> -Password <SecureString>`：生成自签名代码签名证书；
   导入 `Cert:\CurrentUser\Root` 时**会弹 Windows 根证书安全警告，
   需人工确认**；记录输出的指纹。
2. 复制构建产物到临时目录（**绝不签原始构建输出**）。
3. `Sign-ZzLoggArtifacts.ps1 -Files <临时副本.exe> -Thumbprint <指纹>`
   （或 `-PfxPath` + `-Password`）：signtool sign /fd SHA256 签名，
   退出码 0。
4. `Sign-ZzLoggArtifacts.ps1 -Files <临时副本.exe> -Verify`：
   `signtool verify /pa` 验签通过，退出码 0。
5. 篡改已签文件一个字节（如 PowerShell 改写末字节），再次 `-Verify`：
   **验签必须失败**（非零退出）。
6. 清理：删除 `Cert:\CurrentUser\Root\<指纹>` 测试根证书、
   `Cert:\CurrentUser\My` 中同 Subject 测试证书与临时 PFX——不长期
   信任无保护的测试根。

**预期结果（清单原文）：**

> - 生产签名链成功、证书轮换与撤销/离线验收；

本条覆盖该条目的**测试链闭环**部分（签名→验签→篡改拒绝→清理全链路
真实可跑）；生产签名（真实证书、时间戳服务、HSM/流水线凭据）不在本
仓库与本工具链范围内，仍属生产流水线职责。对照红线原文："验证结束
后必须删除 `CurrentUser\Root` 中的测试根证书与临时 PFX，不长期信任
无保护的测试根。"

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 指纹 | 签名退出码 | 验签退出码 | 篡改验签 | Root/My 清理确认 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |  |

---

## §3 其他不可脚本化验收条目

### §3.1 中断恢复（杀引擎进程 / 强制重启）

**前置：** §2.1 链路已可驱动；快照 `baseline-old-installed`。

**逐步操作：**

1. 构造真实升级事务，分别在多个注入点中断：
   a. 备份阶段杀引擎进程（ZzLoggUpdateTx.exe）；
   b. 文件替换阶段杀引擎进程；
   c. 登记写入阶段杀引擎进程；
   d. 替换阶段对 VM 强制关机（宿主机 Power Off，模拟断电），再开机。
2. 每次中断后：确认引擎未报告 Complete、协调者不把中断当成功；现场
   （journal、备份）保留。
3. 经授权路径恢复：受限入口 `/ZzLoggRecover=<定位名>` 驱动引擎恢复，
   断言幂等——重复执行恢复结果一致，退出码为契约值（0 / 44 / 45 /
   46）。
4. journal 损坏注入（可选）：将 journal 截断/置零后恢复，必须呈现
   NeedsAuthorizedRecovery（45）保留现场，绝不自动继续。

**预期结果（清单原文）：**

> - 真实安装目录文件替换事务与中断恢复（杀进程/断电注入点）；

另对照："中断（杀进程/断电点）后经授权路径幂等恢复；……Corrupt 或
0 字节日志呈现为 NeedsAuthorizedRecovery（保留现场，交授权恢复，绝
不自动继续）。退出码：Applied/Recovered/NothingToRecover=0、
Rejected=42、Conflict=43、RolledBack=44、NeedsAuthorizedRecovery=45、
RecoveryFailed=46。"中断后被自动继续、现场被清理、或恢复非幂等，均
判 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 注入点 | 中断方式 | 恢复退出码 | 幂等复核 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §3.2 空间不足注入（RAM 盘 / 小 VHD）端到端

**前置：** §2.1 链路已可驱动；RAM 盘工具（如 ImDisk）或小容量 VHD
（`diskpart` / `New-VHD` 创建并挂载 NTFS）。

**逐步操作：**

1. 用 RAM 盘或小 VHD 制造暂存卷（或目标/备份卷）可用空间小于"包大小
   + 预留"的形态（容量略小于升级事务所需）。
2. 触发升级事务，观察卷空间预检：必须在任何修改前失败关闭
   （Rejected=42 或相应失败分支），无部分写入、无撕裂 journal。
3. 扩容后重跑，确认事务可正常完成（证明拒绝来自空间预检而非其他
   缺陷）。
4. 分别对暂存卷与备份/目标卷注入一次（两侧分别预检）。

**预期结果（清单原文）：**

> - 分卷（暂存与备份不同卷）空间预检端到端（本机单固定盘环境受限）；

另对照："卷空间预检采用饱和加法、1 MiB 日志预留和最近现存非重解析
祖先查询，暂存卷与备份卷分别预检。"空间不足下出现部分替换、登记
推进或误报成功，判 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 注入卷 | 注入方式/容量 | 拒绝形态/退出码 | 扩容复跑 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §3.3 篡改 / 路径替换拒绝

**前置：** §2.1 链路已可驱动，事务暂存目录可构造。

**逐步操作：**

1. 暂存就绪后，修改 staging 载荷一个字节 → 驱动引擎，必须因 SHA-256
   不符拒绝（Rejected=42），无任何安装目录写入。
2. 将 staging 中载荷替换为同名异源文件 → 同样必须拒绝。
3. 路径替换：在暂存/目标路径任一祖先层换入 junction/重解析点 →
   逐级拒绝，失败关闭。
4. 每次拒绝后确认：登记未推进、现场按契约处置、无残留授权。

**预期结果（协议原文）：**

> "大小和哈希一致不构成签名信任。"
> "逐级拒绝重解析点，保持祖先目录稳定租约，刷新并验证目标字节数与
> SHA-256，在 CreateProcess 前再次核验身份。"
> "重放严格 fail-closed：撕裂尾、未知操作、未知 flags、序号缺口、
> 事务 ID 不符均判 Corrupt 且重放为零操作；重放只读幂等。"

篡改或路径替换被接受、或以任何形式推进登记/替换，判 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 篡改方式 | 拒绝退出码 | 写入核查 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

### §3.4 安装登记与用户数据保持手工复核（CI 用例 02/06）

CI 用例 02（registry-identity）与 06（uninstall-keeps-userdata）已在
流水线自动化；本条为真实环境**手工复核**，确认 CI 判据在真实 Windows
评估环境同样成立，并补充 CI 未覆盖的真实使用数据形态。

**前置：** 快照 `baseline-clean`。

**逐步操作：**

1. 真实安装 → `reg query "HKLM\SOFTWARE\Microsoft\Windows
   \CurrentVersion\Uninstall\ZzLogg"` 复核：键存在、
   `UpdateIdentitySchema`=2（DWORD 类型）、`DisplayVersion` 非空、
   `InstallLocation` 与实际安装目录一致。
2. 真实使用产生用户数据：启动应用、打开若干日志文件、保存会话与
   过滤器/收藏（%APPDATA%\ZzLogg 下真实文件，不止 probe.ini 替身）。
3. 控制面板/卸载入口卸载 → 复核：安装目录已删除；%APPDATA%\ZzLogg
   用户数据保留；卸载项键已移除。
4. 升级链路（§2.1）后复核登记推进：DisplayVersion 递增、
   UpdateIdentitySchema 维持 2、卸载入口指向新版本可用。

**预期结果（清单原文）：**

> - 真实 HKLM 写入（卸载项、UpdateIdentitySchema=2）与登记回滚恢复；

对照用例 06 依据："卸载器只删除 ZzLogg.ini / ZzLogg_session.ini 并以
非 /r 的 RMDir 尝试移除用户配置目录"——用户数据被删除即 FAIL。

**记录栏：**

| 日期 | 执行人 | 镜像/快照 | 登记复核 | 用户数据复核 | 卸载复核 | 升级后登记 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### §3.5 长路径与非 NTFS 文件系统（结论记录）

**前置：** 无（本条为结论性验收）。

**协议现状（原文）：**

> "当前仅支持普通盘符绝对路径。UNC、设备/扩展路径（如 `\\?\`）、
> 盘符相对路径、根相对路径、`.`/`..` 分量、重复分隔符、尾随点/空格、
> 额外冒号（含备用数据流）与通配符等歧义拼写会返回 Invalid"
> "长路径和其他文件系统未做实际兼容性验收。"
> "其他文件系统和长路径兼容性仍需另行验收。"

**逐步操作与结论：**

1. 本项目**不声明支持**长路径与非 NTFS：在手册登记"不支持"结论，
   关闭清单条目"长路径与非 NTFS 文件系统兼容性（若声明支持）"。
2. 可选探针（记录但不作为通过条件）：在 >MAX_PATH 路径或 exFAT/
   ReFS 卷上尝试安装/升级链路，预期行为是**失败关闭**（探测返回
   Invalid/Unsupported 或引擎拒绝），绝不允许中途损坏安装目录或
   登记；记录实际观察。若观察到非失败关闭行为，升级为产品缺陷单独
   跟踪，不改本条结论。

**记录栏：**

| 日期 | 执行人 | 结论（不支持/支持） | 可选探针观察 | 结果 | 证据路径/备注 |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |

### §3.6 跨卷预检真实覆盖说明

CI 用例 07（space-preflight-multivolume）断言偏弱：普通安装本身不跑
引擎空间预检（预检属于升级事务，需协调者凭据），用例 07 仅断言"跨卷
安装成功 + %ProgramData%\ZzLogg 下无报告空间不足的日志"。**真实跨卷
暂存预检在 §2.1 链路覆盖**：暂存目录（%ProgramData% 所在卷）与安装
目标/备份卷分离时执行 §2.1，断言暂存卷与备份卷分别预检生效；空间不
足的注入式覆盖见 §3.2。验收汇总时，跨卷预检条目的通过依据是
§2.1+§3.2 的真实记录，不是 CI 用例 07 的 PASS。

---

## §4 CI 首跑检查项（update-smoke.yml 首次运行核对）

流水线 `.github/workflows/update-smoke.yml` 的真实通过证据待推送后在
GitHub 上观察首跑（本阶段不 push）。首跑逐项核对：

1. **首跑用 `workflow_dispatch` 手动触发**（不等 push 路径条件）。
2. **runner 提权状态确认**：看"提权能力探测"输出，预期
   `elevated=true`；若为 `false`，确认 job summary 出现 01–07 排除
   标注（含 7 个用例名）、报告中仅 08–12 SKIP 痕，并按 yaml 注释复核
   filter 策略——不静默跳过、不伪造通过。**非提权分支从未实测**：
   filter 通配已本机实测，但整段非提权路径（双轮调用、报告聚合）
   只能在真实非提权 runner 上验证；预期 windows-2022 不会走到。
3. **agent-setup 存量风险**：`agent-setup` 内部仍引用 cache@v1（Qt）/
   v2（Boost），cache v1/v2 已被 GitHub 停用，首跑可能红在
   agent-setup——这是 ci-build.yml 同源**存量问题，非本阶段引入**；
   若发生，单独决策升级 agent-setup/ci-build.yml，不在本阶段范围。
4. **openssl 缓存**：确认缓存命中/下载路径正常（key
   `OpensslCache-1-1-1w`，新 workflow 用 actions/cache@v4）、`SSL_DIR`
   硬断言未触发（agent-package-win 对 libcrypto/libssl DLL 的硬断言）。
5. **用例 07 双卷依赖**：依赖 runner 双卷 C:+D:——确认其非 SKIP；若
   runner 变单卷，该用例 SKIP 属预期（脚本内判）。
6. **验收报告 artifact 上传确认**：artifact `update-smoke-report`
   上传成功且含 12 条 testcase（7 PASS + 5 SKIP 为全绿标准；FAIL>0
   即红）。
7. 首跑全绿后，以 push 触发路径（`src/updater/**`、
   `packaging/windows/**`、`tools/acceptance/**`、workflow 自身）再
   验证一次自动触发链路。

---

## §5 验收汇总

| 条目 | 对应来源 | 结果 | 证据路径 | 备注 |
| --- | --- | --- | --- | --- |
| §2.1 旧版升级端到端（真实协调者链） | 用例 08 / 清单多条 / 裁决表 #9 #11 #16 #26 |  |  |  |
| §2.2 真实 UAC 提权与用户拒绝 | 用例 09 |  |  |  |
| §2.3 另一管理员账号 | 用例 10 / 裁决表 #25 |  |  |  |
| §2.4 关机/注销与 Prepared 窗口 | 用例 11 |  |  |  |
| §2.5 文件占用升级失败与回滚 | 用例 12 |  |  |  |
| §2.6 签名闭环交互验收 | 控制者裁决新增 / 测试签名工具链 |  |  |  |
| §3.1 中断恢复 | 清单：文件替换事务与中断恢复 |  |  |  |
| §3.2 空间不足注入 | 清单：分卷空间预检 |  |  |  |
| §3.3 篡改/路径替换拒绝 | 3B.2/3C 失败关闭契约 |  |  |  |
| §3.4 登记与用户数据保持复核 | CI 用例 02/06 手工复核 |  |  |  |
| §3.5 长路径与非 NTFS | 清单：长路径与非 NTFS（不声明支持，记录结论） |  |  |  |
| §3.6 跨卷预检真实覆盖 | CI 用例 07 断言偏弱说明 |  |  | 由 §2.1+§3.2 覆盖 |
| §4 CI 首跑检查项 | 任务 6 报告 |  |  |  |
