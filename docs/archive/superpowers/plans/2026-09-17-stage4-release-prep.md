# 阶段 4 准备（发布与上线·环境无关部分）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法跟踪进度。

**目标：** 在不需要真实隔离环境的前提下完成阶段 4 全部可本机/CI 完成的工作：3C 延后 Minor 甄别与本机可修项修复、测试签名工具链、验收脚本包、GitHub Actions 真实 NSIS 冒烟流水线、VM 验收手册。完成后真实环境验收成为"照单执行"。

**架构：** 代码修复沿用 3C 架构、不动协议语义；签名与验收为新增 PowerShell 工具脚本；CI 冒烟复用现有 composite actions 构建打包，新增独立 workflow 调用验收脚本包。windows-2022 runner 本身是管理员权限的真实 Windows 环境（真实 ProgramData/HKLM/双卷 C:+D:），可覆盖阶段 4 清单中安装、登记、ACL、受限入口失败关闭、分卷预检等条目；交互式 UAC、另一管理员账号、关机注销等条目由脚本显式 SKIP 并指向 VM 手册。

**技术栈：** PowerShell 5.1+/7（runner 自带）、GitHub Actions、NSIS 3.x（CI 用 joncloud/makensis-action，本机 makensis 3.11）、signtool（Windows SDK）、CMake/CTest。

**规格（执行者两份都读）：**
- `docs/superpowers/specs/2026-09-15-installer-only-update-design.md` §7（发布与上线门禁）
- `docs/development/UPDATE_PROTOCOL.md`「3C 未交付项与阶段 4 真实环境验收清单」（文件末尾，行 524 起）
- 3C 延后清单（25 条）：主工作区 `.worktrees/update-core/.superpowers/sdd/2026-09-17-install-transaction/progress.md` 中全部 `minor (deferred)` 行（本文件 git-ignored，仅存在于主工作区磁盘，甄别时从该路径读取；行 28-83）

## 全局约束

- 生产入口保持关闭：不新增生产会话工厂、不填生产发布者证书指纹、不配置生产 HTTPS/公钥；"退出并更新"生产可见性不开放（规格 §7：缺失时关闭安装能力）。
- 测试签名仅用自签名证书；任何 `.pfx`/私钥不入库（`.gitignore` 必须覆盖 `*.pfx`）；证书 Subject 必须含 `Test` 字样。
- 不伪造生产配置、不使用仓库测试私钥发布、不在本机写真实 HKLM 或模拟系统级升级（规格 §7 原文）。本机验证脚本只允许操作用户级证书存储与临时目录。
- 协议语义（bootstrap v3、通道消息 v2、Proceed 闸门、ACL 形状断言）不因本计划变更；C++ 修复仅限任务 1 甄别为"本机可修"的延后项。
- TDD 强制：C++ 修复先写失败测试（先红后绿）；PowerShell 脚本以真实运行输出验证。
- NSIS 改动：逐行审分支语义（IntCmp 标签顺序、System::Call 结构体布局），本机 `makensis`（3.11）真实编译通过，并复跑 `windowsinstallercontracttest`。
- 构建：Git Bash 中先 `export CL='-MP8'`（不是 `/MP8`），再 `D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8`；测试 `D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure`。工作树内的构建目录用 `out/ui-vs`（工作树首次需 configure：复用主工作区 CMakeUserPresets 的 ui-vs preset）。
- 已知环境 flake：restart_contract 偶发 300s 挂起 0xc0000409、storage_migrator 符号链接失败——低负载复跑即过，与更新功能无因果，勿靠重试销案，记录即可。
- 提交全部中文（标题 + 详细正文）；工作分支 `codex/stage4-release-prep` @ `D:/File/Program/GitCode/ZzLogg/.worktrees/stage4-release`；全部完成并审查通过后 ff-only 合并 master，不 push。
- CI workflow 改动不得改变现有 `ci-build.yml` 的行为；新 workflow 用独立文件。

---

### 任务 1：3C 延后 Minor 甄别与裁决表

**文件：**
- 创建：`docs/development/UPDATE_STAGE4_TRIAGE.md`
- 读取：主工作区 `.worktrees/update-core/.superpowers/sdd/2026-09-17-install-transaction/progress.md` 行 28-83（25 条 `minor (deferred)`）
- 读取（按条目引用逐一核对现状）：`src/updater/coordinator.cpp`、`src/updater/txjournal_win.cpp`、`src/updater/txengine_win.cpp`、`src/updater/txengine_main.cpp`、`src/updater/bootstrap_win.cpp`、`packaging/windows/ZzLogg.nsi`、`packaging/windows/GenerateNsisManifest.ps1`

- [ ] **步骤 1：核对 25 条延后项的当前代码状态**

对 progress.md 行 28-83 每一条 `minor (deferred)`，打开其引用的文件与行号，确认问题在当前代码中仍然存在、已被修复、或描述已过时。特别注意：`coordinator.cpp:662-666` 启动确认忙等条目——提交 `51343429`（"fix: 修正 NSIS 事务目录冲突与启动确认忙等"）声称已修，必须读代码核实是否真修（`git -C 主工作区 show 51343429 -- src/updater/coordinator.cpp`）。

- [ ] **步骤 2：对每条给出裁决**

三选一，并写一句理由：
- **本批修复**（纯代码/脚本问题，本机可修可测）→ 进入任务 2 或任务 3；
- **真实环境验收**（依赖真实 UAC/HKLM/ProgramData/多卷/多账号）→ 进入任务 5 验收用例或 VM 手册条目；
- **不修**（风险已接受/输入受控/测试专用）→ 写明接受理由。

预期裁决起点（任务 1 可依据代码现状调整，调整必须在表中写明理由）：
- 本批修复候选：txjournal append 对称上限、replay 的 FILE_SHARE_WRITE、coordinator restart() 一次性闸门、restarted.adopt 句柄泄漏、start() adopt 失败早退、proceedSent 短路顺序、GenerateNsisManifest.ps1:82 二次 stat、ZzLogg.nsi:247 FileOpen 缺 IfErrors、退出码契约重复（exitForOutcome/parseHex/SID 三处副本单一来源化）
- 真实环境验收候选：SDDL owner 确认、ACL BUILTIN\Users vs Authenticated Users、跨完整性级别令牌、引擎 accept 路径、分卷预检、提权 harness 覆盖、孤儿备份清理的端到端验证
- 不修候选：spawnMappedFixture 句柄继承（测试专用）、bootstrap 控制字符检查（数据面足够）、TOCTOU 低危窗口（祖先已钉住）、prepare() 无哈希预检（回滚兜底）、recover 仅查 journal.log（引擎侧复核）、marker 仅存在性检查（分工如此）、manifest 自列表项（生产者受控）

- [ ] **步骤 3：写裁决表文档**

`docs/development/UPDATE_STAGE4_TRIAGE.md`，格式：每条一行表格 `| # | 位置 | 问题摘要 | 裁决 | 去向（任务/用例）| 理由 |`，25 行一行不少；表头写明来源（3C progress.md 路径与日期）。文末附"已核实修复"小节列出核对中发现已修复的条目及证据提交号。

- [ ] **步骤 4：自检覆盖度**

运行：`grep -c "minor (deferred)" "D:/File/Program/GitCode/ZzLogg/.worktrees/update-core/.superpowers/sdd/2026-09-17-install-transaction/progress.md"`，结果必须等于裁决表行数（25）。缺一即漏。

- [ ] **步骤 5：Commit**

```bash
git add docs/development/UPDATE_STAGE4_TRIAGE.md
git commit -m "docs: 甄别 3C 延后 Minor 并给出阶段 4 裁决表

<正文：25 条总数、三类裁决各多少条、已核实修复条目、调整预期裁决的理由>"
```

---

### 任务 2：代码级延后项修复批次 A（updater C++ 核心）

**文件（以任务 1 裁决表为准，以下为预期全集）：**
- 修改：`src/updater/txjournal_win.cpp`（append 对称上限；replay 去 FILE_SHARE_WRITE，行 202/226 附近）
- 修改：`src/updater/coordinator.cpp`（restart() 一次性闸门 行 628；restarted.adopt 句柄泄漏 行 650-653；start() adopt 失败早退 行 34-36；proceedSent 短路顺序 行 66）
- 修改：`src/updater/txengine_main.cpp`、`src/updater/txengine_win.cpp` 及对应测试/fixture（退出码契约 exitForOutcome/parseHex/SID 单一来源化）
- 测试：`tests/unit/`、`tests/updater/` 中对应测试文件（先找到现有测试再加）

**每小项统一流程（TDD）：**

- [ ] **步骤 1：写失败测试**

针对该延后项的缺陷行为写断言（例如：journal append 超过 replay 上限时 append 自身被拒绝；restart() 第二次调用不启动第二个 GUI 实例；replay 打开 journal 不带 FILE_SHARE_WRITE——用注入/mock seam 观测）。测试命名沿用所在测试文件既有风格。

- [ ] **步骤 2：运行确认失败（先红）**

```bash
export CL='-MP8'
D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8 --target <相关测试目标>
D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R <测试名> --output-on-failure
```
预期：FAIL，失败信息指向该缺陷。

- [ ] **步骤 3：最小实现修复**

只改使测试转绿所需的最小代码。不改变任何对外协议语义（全局约束）。append 上限的数值必须与 replay 侧常量单一来源（同一头文件常量），不得两处各写一份字面量。

- [ ] **步骤 4：运行确认通过（后绿）+ 变异测试**

复跑同一命令预期 PASS；再把修复临时注释掉确认测试回红（变异验证），随后恢复。

- [ ] **步骤 5：单小项 commit**

```bash
git commit -m "fix: <该延后项一句话>

<正文：缺陷、修复方式、对应 3C 延后条目编号、变异验证结果>"
```

- [ ] **步骤 6：批次收尾**

全部小项完成后跑受影响测试全集（`ctest -R "updater|coordinator|txengine|txjournal"`），全绿后进行批次审查。

---

### 任务 3：代码级延后项修复批次 B（PowerShell 与 NSIS）

**文件：**
- 修改：`packaging/windows/GenerateNsisManifest.ps1`（行 82：第二次 stat 改为复用 `$fileStream.Length`）
- 修改：`packaging/windows/ZzLogg.nsi`（行 247：`FileOpen` 写 nsis-entry.log 补 `IfErrors` 分支，失败仅跳过诊断日志、不改变主流程退出码）
- 修改：`packaging/windows/ZzLogg.nsi` 或 `src/updater/txengine_win.cpp`（仅任务 1 裁决为"本批修复"的注释/分工类条目：VerifyTarget 分工注释、提权孤儿窗口注释等）
- 测试：`tests/ui_acceptance/windowsinstallercontracttest.cmake`（复跑，不改其断言——除非裁决表明确要求）

- [ ] **步骤 1：逐行审 ZzLogg.nsi 待改分支语义**

读行 247 前后完整函数，确认 FileOpen/FileWrite/FileClose 顺序与 `$0` 等寄存器占用；补 IfErrors 不得吞掉主流程错误码。

- [ ] **步骤 2：修改 + makensis 真实编译**

```bash
cd packaging/windows
/c/PROGRA~2/NSIS/makensis.exe /NOCD -DVERSION=test -DPLATFORM=x64 ZzLogg.nsi   # 本机 makensis 3.11；路径以实际为准（which makensis 先探测）
```
预期：编译 0 错误。（若编译需要 staging 树，则只做语法级验证：`makensis /NOCD -DVERSION=test -DPLATFORM=x64` 对缺少输入文件报的是 File 错误而非语法错误——需区分；必要时先按 `.github/actions/agent-package-win/action.yml` 的 staging 步骤准备 release/ 树再编译。）

- [ ] **步骤 3：复跑 NSIS 契约测试**

```bash
D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R "windowsinstaller" --output-on-failure
```
预期：PASS。

- [ ] **步骤 4：GenerateNsisManifest.ps1 修复 + 真实运行验证**

改行 82 后，对 `test_data/` 目录真实运行该脚本，diff 修复前后生成的清单必须逐字节一致（行为不变重构）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/GenerateNsisManifest.ps1 -StagingDirectory <临时副本>
```

- [ ] **步骤 5：Commit**

```bash
git commit -m "fix: 修复 NSIS 与清单脚本的 3C 延后项

<正文：各小项、makensis 编译结果、契约测试结果、清单逐字节一致性验证>"
```

---

### 任务 4：测试签名工具链（自签名证书 + signtool 脚本）

**文件：**
- 创建：`packaging/windows/New-ZzLoggTestCertificate.ps1`
- 创建：`packaging/windows/Sign-ZzLoggArtifacts.ps1`
- 修改：`.gitignore`（确认/追加 `*.pfx`）
- 文档：在 `docs/development/UPDATE_PROTOCOL.md` 阶段 4 章补"测试签名"小节（本任务步骤 5）

**`New-ZzLoggTestCertificate.ps1` 行为规格：**
- 参数：`-ExportPfx <path>`（可选）、`-Password <SecureString>`（导出 PFX 时必填）、`-TrustCurrentUser`（开关，导入 CurrentUser\Root 使本机验签通过）
- 核心逻辑：

```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=ZzLogg Test Code Signing (NOT FOR PRODUCTION)" `
    -CertStoreLocation Cert:\CurrentUser\My `
    -KeyExportPolicy Exportable `
    -NotAfter (Get-Date).AddYears(2) `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
# -TrustCurrentUser 时：
Export-Certificate -Cert $cert -FilePath "$env:TEMP\zzlogg-test-root.cer"
Import-Certificate -FilePath "$env:TEMP\zzlogg-test-root.cer" -CertStoreLocation Cert:\CurrentUser\Root
# -ExportPfx 时：
Export-PfxCertificate -Cert $cert -FilePath $ExportPfx -Password $Password
```

- 输出：证书指纹（Thumbprint）+ 醒目警告"仅测试用途，禁止用于发布"；重复运行时若同 Subject 证书已存在则复用并打印指纹（幂等）。
- 只写 `Cert:\CurrentUser\*`，绝不写 LocalMachine（全局约束：不模拟系统级变更）。

**`Sign-ZzLoggArtifacts.ps1` 行为规格：**
- 参数：`-Files <path[]>`、`-PfxPath` / `-Thumbprint`（二选一）、`-Password`、`-TimestampUrl`（可选，默认不加时间戳以支持离线）、`-Verify`（开关，只验签不签名）
- signtool 定位顺序：`Get-Command signtool` → vswhere 探测 → `${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe` 取最新版；找不到时报错退出码 2 并提示安装 Windows SDK。
- 签名：`signtool sign /fd SHA256 /f $PfxPath /p $pwd`（或 `/sha1 $Thumbprint`），加 `/tr $TimestampUrl /td SHA256` 仅当指定了 TimestampUrl；任何文件失败即非零退出并列出失败文件。
- 验签：`signtool verify /pa`（需 `-TrustCurrentUser` 导入过根）；逐文件打印结果。
- 签名前对每个文件检查扩展名 ∈ {`.exe`, `.dll`, `.msi`}，否则拒绝（防误签）。

- [ ] **步骤 1：写两个脚本**

按上规格实现，文件头注释写明"仅测试签名，发布签名由生产流水线与真实证书承担"。

- [ ] **步骤 2：`.gitignore` 检查**

`Grep` 仓库根 `.gitignore` 是否已有 `*.pfx`；没有则追加一行 `*.pfx`。

- [ ] **步骤 3：端到端真实验证（本机）**

```powershell
# 1. 生成证书并导入 CurrentUser\Root
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/New-ZzLoggTestCertificate.ps1 -TrustCurrentUser
# 2. 复制一个构建产物 exe 到临时目录做签名对象（不签原始构建输出）
# 3. 签名
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/Sign-ZzLoggArtifacts.ps1 -Files "$env:TEMP\sign-test\ZzLoggUpdateTx.exe" -Thumbprint <步骤1打印的指纹>
# 4. 验签必须通过
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/Sign-ZzLoggArtifacts.ps1 -Files "$env:TEMP\sign-test\ZzLoggUpdateTx.exe" -Verify
# 5. 阴性：篡改一个字节后验签必须失败
```
预期：步骤 4 退出 0；步骤 5 退出非零。记录全部输出到 commit 正文或任务报告。

- [ ] **步骤 4：清理**

删除 CurrentUser\Root 中的测试根证书（`Remove-Item Cert:\CurrentUser\Root\<指纹>`）与临时 PFX，保持本机不留测试信任（避免长期信任一个无保护的测试根）。

- [ ] **步骤 5：文档 + Commit**

UPDATE_PROTOCOL.md 阶段 4 章补"测试签名"小节：两个脚本的用法、仅测试用途红线、生产签名由任务外流水线承担的边界。Commit：

```bash
git commit -m "feat: 新增测试签名工具链（自签名证书与 signtool 脚本）

<正文：脚本职责、端到端验证证据（验签通过/篡改失败）、清理确认、仅测试用途红线>"
```

---

### 任务 5：验收脚本包（tools/acceptance/）

**文件：**
- 创建：`tools/acceptance/Invoke-UpdateAcceptance.ps1`（运行器）
- 创建：`tools/acceptance/cases/*.ps1`（每用例一个文件）
- 测试方式：本机以"无安装包"模式运行验证 SKIP/FAIL 语义；完整 PASS 由任务 6 在 CI 真实安装后验证

**运行器规格：**

```powershell
# Invoke-UpdateAcceptance.ps1
param(
  [string]$SetupExe,        # 安装包路径；缺省时所有需要安装包的用例 SKIP
  [string]$InstallDir,      # 默认 "$env:ProgramFiles\ZzLogg"（CI 传显式路径）
  [string]$Filter = "*",    # 用例名通配
  [string]$ReportPath       # 可选，写 junit-ish 文本报告
)
# 行为：dot-source cases/*.ps1，逐个调用，收集结果对象
#   @{ Name; Status = PASS|FAIL|SKIP; Evidence; ElapsedMs }
# 每行输出：CASE <name>: <STATUS> - <evidence>
# 退出码 = FAIL 数（SKIP 不算失败）；结束打印汇总 N PASS / M FAIL / K SKIP
```

**用例清单（每个一个 `cases/NN-<name>.ps1`，PASS 判据逐字如下）：**

| # | 用例 | CI 可跑 | PASS 判据 |
|---|------|---------|-----------|
| 01 | install-silent | 是 | `setup.exe /S` 退出 0；`$InstallDir\ZzLogg.exe`、`.zzlogg-install-root`、`.zzlogg-files.manifest` 存在；manifest 首行 schema=2 |
| 02 | registry-identity | 是 | `HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg` 存在且 `UpdateIdentitySchema`=2（DWORD）；DisplayVersion 非空 |
| 03 | protected-root-acl | 是（管理员） | `%ProgramData%\ZzLogg\UpdateTransactions` 存在；Get-Acl 显示属主 ∈ {Administrators, SYSTEM}；除 Administrators/SYSTEM 外无任何写位 ACE；Authenticated Users 或 Users 仅只读——与引擎断言同一规则 |
| 04 | upgrade-entry-badargs | 是 | `setup.exe /ZzLoggUpgrade=garbage`（及缺值、不存在路径两种变体）退出码非 0；`$InstallDir` 内容哈希前后一致；无新增 staging 目录残留 |
| 05 | recover-entry-nojournal | 是 | 无 journal.log 时 `setup.exe /ZzLoggRecover=<dir>` 退出码非 0，且不改动 `$InstallDir` |
| 06 | uninstall-keeps-userdata | 是 | 安装→写入 `%APPDATA%\ZzLogg\probe.ini`→静默卸载→`probe.ini` 仍在；`$InstallDir` 已删除 |
| 07 | space-preflight-multivolume | 是（runner 有 C:/D:） | 用 `/D=` 装到 D: 卷（普通安装不禁 /D=，仅受限入口禁）→ 制造暂存（C: ProgramData）与目标（D:）跨卷场景，断言安装成功且预检日志无空间误报；单卷机器上 SKIP |
| 08 | upgrade-endtoend | 否 | SKIP（需协调者生产链路真实凭据 + 旧版应用，CI 无法构造活协调者身份）→ VM 手册条目 |
| 09 | uac-cancel | 否 | SKIP（CI 无交互桌面）→ VM 手册条目 |
| 10 | multi-admin-account | 否 | SKIP → VM 手册条目 |
| 11 | session-end-queryendsession | 否 | SKIP（需真实关机/注销交互）→ VM 手册条目 |
| 12 | file-in-use-upgrade | 否 | SKIP（依赖用例 08 的真实升级链）→ VM 手册条目 |

- SKIP 用例的 Evidence 必须写明跳过原因与对应 VM 手册章节号。
- 每个用例自清理（安装过的卸载掉、创建的目录删掉），失败也要清理（try/finally）。

- [ ] **步骤 1：实现运行器与用例 01-07**

- [ ] **步骤 2：实现 SKIP 用例 08-12**（固定 SKIP 语义 + 手册指引文案）

- [ ] **步骤 3：本机无包装验证**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/acceptance/Invoke-UpdateAcceptance.ps1
```
预期：01-07 因无 SetupExe 全部 SKIP（不是 FAIL），退出码 0；再传一个不存在路径 `-SetupExe C:\nonexistent.exe`，预期 01-07 FAIL、退出码 = FAIL 数（验证失败语义不哑）。

- [ ] **步骤 4：Commit**

```bash
git commit -m "feat: 新增更新验收脚本包（12 用例，7 可 CI 自动化）

<正文：用例清单与判据、本机 SKIP/FAIL 语义验证输出、SKIP 用例指向 VM 手册>"
```

---

### 任务 6：CI 冒烟流水线（update-smoke.yml）

**文件：**
- 创建：`.github/workflows/update-smoke.yml`
- 修改：`.github/actions/agent-package-win/action.yml`（s3-key-id/s3-secret/s3-bucket 三个 input 改 `required: false`——实际未使用，仅声明；这是让新 workflow 能在无签名机密的 fork/PR 上复用打包步骤的最小改动）
- 调用：任务 5 的 `tools/acceptance/Invoke-UpdateAcceptance.ps1`

**workflow 规格（关键骨架）：**

```yaml
name: "Update Smoke"
on:
  workflow_dispatch:
  push:
    branches: [ master ]
    paths:
      - 'src/updater/**'
      - 'packaging/windows/**'
      - 'tools/acceptance/**'
      - '.github/workflows/update-smoke.yml'

jobs:
  Smoke:
    runs-on: windows-2022
    env:
      QT_VERSION: 6.8.3
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/klogg-version
      - uses: ./.github/actions/prepare-workspace-env
      - uses: ilammy/msvc-dev-cmd@v1
        with: { arch: x64 }
      - uses: ./.github/actions/agent-setup
      - uses: ./.github/actions/agent-build
      - uses: ./.github/actions/agent-package-win   # s3 输入改可选后无需传
      - name: 提权能力探测
        id: elev
        shell: powershell
        run: |
          $id = [Security.Principal.WindowsIdentity]::GetCurrent()
          $p = New-Object Security.Principal.WindowsPrincipal($id)
          echo "elevated=$($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))" >> $env:GITHUB_OUTPUT
      - name: 验收冒烟
        shell: powershell
        run: |
          $setup = Get-Item "ZzLogg-*-setup.exe" | Select-Object -First 1
          powershell -NoProfile -ExecutionPolicy Bypass -File tools/acceptance/Invoke-UpdateAcceptance.ps1 `
            -SetupExe $setup.FullName -ReportPath smoke-report.txt
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: update-smoke-report
          path: smoke-report.txt
          if-no-files-found: warn
```

- 构建/打包步骤的环境变量（KLOGG_BUILD_ROOT、SSL_DIR 等）必须与 `ci-build.yml` Windows job 一致——实现时逐行对照该 job（openssl 缓存/下载步骤同样照搬，打包脚本对 SSL_DIR 有硬断言）。
- 若 runner 未提权，03/07 等需要管理员的用例会 FAIL——探测结果 elevated=false 时以 `-Filter` 排除需提权用例并在总结中标注（失败关闭原则：能跑的必须全绿，不能跑的显式排除并留痕，不允许静默跳过）。实现时先在 PR/分支上跑一次确认 runner 提权状态，再定 filter 策略写进 yaml 注释。

- [ ] **步骤 1：改 agent-package-win input 为可选 + 对照 ci-build.yml 写 workflow**

- [ ] **步骤 2：静态校验**

`actionlint`（若本机可用，`where actionlint` 探测；不可用则人工逐行核对 yaml 语法与现有 workflow 风格一致性）。确认 ci-build.yml 零改动（`git diff` 为空——本任务不改它）。

- [ ] **步骤 3：Commit**

```bash
git commit -m "ci: 新增更新冒烟流水线（真实 NSIS 安装/受限入口/ACL 验收）

<正文：触发条件、复用的 composite actions、agent-package-win input 可选化理由、验收脚本调用与报告上传、提权探测与 filter 策略>"
```

- [ ] **步骤 4（留待推送后）：** 本任务的真实通过证据需要推到 GitHub 后观察 workflow 运行。合并 master 后不 push（全局约束）；在任务报告中明确标注"CI 真实运行待用户推送后确认"，并在 VM 手册列出首跑检查项。

---

### 任务 7：VM 验收手册与文档收口

**文件：**
- 创建：`docs/development/UPDATE_ACCEPTANCE_VM.md`
- 修改：`docs/development/UPDATE_PROTOCOL.md`（阶段 4 章更新进展：本计划交付物清单、裁决表/手册链接、剩余纯真实环境条目）

**VM 手册内容（每项含：前置、逐步操作、预期结果、记录栏）：**
1. 环境获取：微软官方免费 Windows 11 评估 VM（developer.microsoft.com/windows/downloads/virtual-machines，Hyper-V/VirtualBox 镜像）或 90 天 Enterprise 评估 ISO；快照基线。
2. 手工验收条目（对应用例 08-12 及 UPDATE_PROTOCOL 清单中不可脚本项）：
   - 真实 UAC 提权链路 + 用户拒绝路径（ERROR_CANCELLED→Cancelled 映射观察）
   - 旧版升级端到端：装旧版→造更新清单/载荷→走真实协调者链→断言替换+登记+重启
   - 另一管理员账号下的升级（跨完整性级别令牌验证）
   - 中断恢复：升级中途杀引擎进程/强制重启→重进系统后恢复或回滚断言
   - 文件占用升级失败与回滚
   - 关机/注销与 Prepared 窗口交互抽查
   - 空间不足注入（用 RAM 盘或小 VHD 制造）端到端
   - 篡改/路径替换（改 staging 载荷一字节）拒绝断言
   - 安装登记与用户数据保持复核（脚本 02/06 的手工复核）
   - 长路径与非 NTFS（若声明支持；不声明则记录"不支持"结论）
3. 每条的预期结果必须引用 UPDATE_PROTOCOL.md 对应清单条目原文。
4. 附"CI 首跑检查项"小节（任务 6 步骤 4）。

- [ ] **步骤 1：写 VM 手册**

- [ ] **步骤 2：更新 UPDATE_PROTOCOL.md 阶段 4 进展**

在「3C 未交付项与阶段 4 真实环境验收清单」小节后追加"阶段 4 进展"段：本计划 7 任务交付物、25 条延后项裁决去向（链接裁决表）、可 CI 自动化条目已脚本化、剩余纯真实环境条目见 VM 手册。**不得删除或弱化原清单条目原文。**

- [ ] **步骤 3：Commit**

```bash
git commit -m "docs: 新增 VM 验收手册并收口阶段 4 进展

<正文：手册覆盖条目与清单映射、UPDATE_PROTOCOL 更新点>"
```

---

### 任务 8：整阶段最终审查与合并

- [ ] **步骤 1：整阶段独立审查**（subagent-driven-development 最终审查子代理，范围：本分支全部 diff vs master，对照本计划全局约束逐条核）

- [ ] **步骤 2：工作树全量验证**

```bash
export CL='-MP8'
D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
```
预期：构建退出 0，CTest 全绿（109/109 基线 + 本阶段新增测试）。

- [ ] **步骤 3：ff-only 合并 master + 主线重验**

```bash
git -C D:/File/Program/GitCode/ZzLogg merge --ff-only codex/stage4-release-prep
# 主工作区重编译 + 全套 CTest，证据日志落 out/ui-vs/stage4-master-{build,tests}.log
```

- [ ] **步骤 4：docs 验收提交 + 报告**

主线 docs 提交（中文）记录阶段 4 准备完成与剩余真实环境验收清单；**不 push**。向用户报告：交付物清单、验证证据、剩余待真实环境事项、推送待命状态。

---

## 自检记录

- **规格覆盖度：** 规格 §7 生产门禁（不伪造配置/测试私钥）→ 全局约束 + 任务 4 红线；§7 真实验收清单 12 项 → UPDATE_PROTOCOL 清单 10 项 → 任务 5 用例 01-07（可脚本化）+ 任务 7 VM 手册（交互/多账号/断电/占用等不可脚本项）；25 条延后 Minor → 任务 1-3；生产签名/HTTPS/发布配置仍缺 → 保持关闭（全局约束第 1 条），CI 冒烟（任务 6）先行覆盖"通过源码契约测试不等于通过真实安装测试"的可自动化部分。
- **占位符扫描：** 各任务步骤均含具体文件/行号/命令/判据；任务 2 的最终小项集以任务 1 裁决表为准（已在任务 1 步骤 2 写明调整规则），非占位符。
- **类型一致性：** 验收脚本运行器参数（-SetupExe/-InstallDir/-Filter/-ReportPath）在任务 5 定义、任务 6 调用一致；签名脚本参数（-Thumbprint/-PfxPath/-Verify/-TrustCurrentUser）任务 4 内部一致。

---

## 主线交付记录（2026-09-18）

**执行方式**：subagent-driven-development，逐任务实现 + 独立审查 + 修复轮 + 整分支最终审查；SDD 账本（git-ignored）：`.worktrees/stage4-release/.superpowers/sdd/2026-09-17-stage4-release-prep/progress.md`。

**任务完成**：任务 1（裁决表 160ecf3c）、2（C++ 批次 a8ad199a..f74dd5b7）、3（PS/NSIS 批次 dd9545bd..1130eeb1）、4（签名工具链 ecfd73aa）、5（验收脚本包 86ca70f8）、5.5（产品侧契约缺口修复 777bc0ee..5729ef3a，审查暴露后新增）、6（CI 冒烟 2f3a65e3）、7（VM 手册 4d52cd94）。最终审查（21 提交全分支）：无 Critical/Important，12 条延后 Minor 甄别无一承重。

**关键裁决**（详见账本）：3C 台账延后条目实为 28 条（"25"系行文数字）；#6 FILE_SHARE_WRITE 实证为并发 replay 承重语义改判不修；NSIS 受限入口 8 处 Abort 补退出码（2/42/48）+ MessageBox /SD；引擎 ACL 写位收窄为显式枚举 0x500D0116（剔除 SYNCHRONIZE/READ_CONTROL，修复引擎必拒安装器自建根的 100% 生产阻断）；新增契约码位 48 InstallerRuntimeFailure。

**验证证据**：工作树构建 exit 0、CTest 108/109（唯一失败 zzlogg_update.release_configuration 为工作树路径深度 263>MAX_PATH 260 的环境问题，分支对 tests/update/ 零改动，主线实证通过）；合并后主线 Release 构建 exit 0（`out/ui-vs/stage4-master-build.log`）、全套 CTest **109/109**（154.06 秒，`out/ui-vs/stage4-master-tests.log`）。

**合并**：ff-only `352ff0c7..4d52cd94`，未推送（github 远程已配，待用户指令）。

**剩余事项**（真实环境/推送后）：update-smoke.yml 首跑确认（VM 手册 §4 检查项，含 agent-setup cache@v1/v2 存量停用风险）；VM 手册 §2/§3 全部交互验收条目（含签名闭环 §2.6）；生产签名证书与发布配置（生产入口保持关闭直至全部通过）。
