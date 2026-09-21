# Windows 稳定版一键安装更新设计

日期：2026-09-21
状态：已批准，待书面规格复核

## 目标

让稳定版渠道的 Windows x64 注册安装用户在"检查更新"对话框点击"退出并安装更新"后，
由程序自动完成下载、校验、提权、事务替换与重启，不再需要前往 GitHub 手动下载安装包。

本设计只接通生产执行路径，不新造安装机制：受限事务引擎 `ZzLoggUpdateTx`、带写前日志
与回滚的 `TxEngine`、协调者 `Coordinator::startInstaller`、Inno Setup 的
`/ZzLoggUpgrade=<locator>` 受限入口都已存在并有测试覆盖，本次补齐它们在生产路径上的
组合、缺失的 bootstrap 字段与被证书策略挡住的包校验。

## 非目标

- 预览渠道一键安装。预览构建常常共用同一显示版本，`selectUpdate` 不把同版本的新每日
  构建判为可升级，接通后按钮大多数时候不会亮；要让它有意义必须先改预览渠道的升级判定，
  那是独立工作。
- Windows 代码签名与发布者指纹校验。项目当前没有代码签名证书，安装时 UAC 会显示
  "未知发布者"，这是本次明确接受的限制。
- macOS 与 Linux 的程序内安装。这两个平台继续使用发布页手动下载。
- 卸载、修复、降级与跨渠道切换。

## 当前阻塞点

接通前，生产路径上有四处独立的闸门同时关闭：

1. **发布身份从未启用。** `ZZLOGG_OFFICIAL_RELEASE` 在任何工作流里都没有打开，
   `compiledReleaseIdentity()` 恒为 `std::nullopt`。
2. **已安装发布身份未接线。** `KloggApp::ensureUpdateService` 给 `UpdateService` 和
   `UpdateDownloadService` 传的 `installed` 都是 `std::nullopt`，`selectUpdate` 因此
   永远走不到 `DecisionStatus::Available`，"下载更新"也始终不可用。
3. **包校验被证书策略硬闸挡住。** `verifyPackageForExecution` 中发布者指纹列表故意留空，
   任何生产输入直接返回 `PublisherPolicyMissing`。
4. **生产执行未组合。** `src/updater/main.cpp` 握手后返回 `ExecutionDisabled`；
   `ApplicationUpdateHandoff` 的生产 `SessionFactory` 为空，`executionAvailable()` 恒为
   false，于是界面显示"此版本尚未启用安装更新"。

## 信任根裁决

没有 Authenticode 证书时，能证明"这个安装包就是本项目发布的那一个"的唯一依据是
Ed25519 签名清单中的字节大小与 SHA-256。清单本身已经过签名验证、有效期检查、渠道绑定
与防重放，下载器也已按清单校验哈希；本次改变的是这份校验结果的授权范围：它从"允许显示
版本信息"升级为"允许执行安装"。

`verifyPackageForExecution` 的改动只有一处：删除"发布者指纹列表为空即拒绝"的硬闸，改为
列表非空时才执行 Authenticode 校验。以下强制项一项不减：

- `revalidateUpdateSelection` 重新验签并复核选择；
- `TrustEnvironment::Production` 环境要求；
- 文件大小与 SHA-256 逐字节校验；
- 打开期间的文件身份租约 `identityUnchanged()`。

`PackageVerificationError::PublisherMismatch` 与 `PublisherPolicyMissing` 枚举值保留，
既有客户端与测试仍在使用。将来获得证书后，只需向指纹列表填入叶证书指纹，双重校验自动
生效，不需要再改代码。

## 组件设计

### A. 发布身份上线

Publish 工作流的 Windows 正式发布构建传入：

```
-DZZLOGG_OFFICIAL_RELEASE=ON
-DZZLOGG_RELEASE_SEQUENCE=<去掉点号的显示版本，如 26.09.03 对应 260903>
-DZZLOGG_RELEASE_CHANNEL=stable
-DZZLOGG_RELEASE_DATA_SCHEMA=0
```

`releaseSequence` 的算法与 `scripts/ci/publish_update_feed.py` 生成清单时一致，两侧必须
得出同一个数，否则 `selectUpdate` 会判为 `ReleaseConflict`。`dataSchema` 取 0，与
`publish_update_feed.py` 中写死的 `--min-data-schema 0 --max-data-schema 0` 对齐。

PR 构建、普通 CI 构建与本地构建保持 `OFF`，不允许非发布构建取得官方发布身份。
`cmake/ZzReleaseIdentity.cmake` 已有的校验（Windows、x64、显示版本形状、tweak 为零、
sequence 非零）继续作为配置期防线，不放宽。

### B. 已安装发布身份接线

新增 Windows 系统版本探测单元（`RtlGetVersion`，失败时返回全零），因为
`makeInstalledRelease` 要求 `osVersion.major != 0`。

`KloggApp::ensureUpdateService` 用 `compiledReleaseIdentity()`、
`probeCurrentInstallation()` 与该系统版本构造 `InstalledRelease`，传给 `UpdateService`
与 `UpdateDownloadService`。三项输入任意一项不满足（非官方构建、非注册安装、探测失败）
时保持 `std::nullopt`，行为与今天完全一致——显示版本信息并引导到 GitHub 手动下载。

这一步单独就能让"下载更新"在正式安装上可用，与后续的执行组合解耦。

### C. 协调者生产组合

生产链路的进程关系：

```
ZzLogg.exe（GUI，持有安装预留）
  └─ Coordinator::start()  ──→  运行时副本 ZzLoggUpdate.exe（协调者，活过 GUI 退出）
                                   └─ Coordinator::startInstaller()
                                        ──→ Inno 安装器 /ZzLoggUpgrade=<locator>（ShellExecuteEx runas，UAC）
                                              └─ ZzLoggUpdateTx.exe（事务引擎，经凭据文件回连协调者）
```

协调者必须是独立进程：`proceedIfExited` 要求在应用进程真实退出后才向引擎放行 Proceed，
GUI 自己无法观测自己的退出。运行时副本的 base 目录取用户缓存下的
`updates/production/runtime-v1`，与既有下载缓存同级，仍是普通用户权限目录，不承载任何
安装授权。

**bootstrap v3 升至 v4。** 现有 `BootstrapData` 只带 `dataDirectory`，协调者无从得知要
启动哪个安装包、目标安装根是什么。v4 增加：

- `installerPath`：已校验安装包的绝对路径，容量与校验规则比照 `dataDirectory`；
- `installRoot`：注册安装目录的绝对路径，同上；
- `expectedSize` 与 `expectedSha256`（32 字节）：来自签名清单的期望字节，协调者在启动
  安装器之前自行复核一遍，不把"父进程说它校验过了"当作依据。

`ChildBootstrap::open` 继续严格校验版本号，`version != 4` 一律拒绝。跨构建失败关闭语义
必须保持：事务引擎编译进新安装包、协调者来自已安装的旧构建，任何版本不匹配都拒绝而不是
尝试兼容，这条约束在 [UPDATE_PROTOCOL.md](../../development/UPDATE_PROTOCOL.md) 的
"发布身份 updaterProtocol 与线协议号的关系"一节已有裁决，本次不放宽。

**`src/updater/main.cpp` 从拒绝改为中继。** 现在它握手后发 `Failed` 并返回
`ExecutionDisabled`。改为：

1. 复核 bootstrap 携带的安装包字节，不符即 fail-closed；
2. `Coordinator::startInstaller` 启动提权安装器，`application` 取 bootstrap 中的父进程
   身份，UAC 拒绝映射为 `LaunchError::Cancelled`；
3. `authenticate` 引擎，`awaitAppExit` 拿到引擎的 `AwaitingAppExit` 后，向 GUI 父通道
   转发 `AwaitingAppExit`；
4. 收到 GUI 的 `CommitExit` 后向引擎 `commitExit`；
5. `proceedIfExited` 等待 GUI 进程真实退出后放行 Proceed；
6. `finish` 取得 `Complete` 后 `restart`，以原用户身份重启并做启动确认。

任一步失败都 fail-closed：不提交、不重启、保留事务备份供授权恢复，退出码沿用
`txcontract_win_p.h` 的单一来源契约。

### D. 生产会话工厂与界面

`KloggApp::updateHandoff()` 传入真实 `SessionFactory`。工厂在以下条件全部成立时才返回
会话，否则返回空指针，`executionAvailable()` 自然保持 false：

- 目标平台为 Windows x64；
- `probeCurrentInstallation()` 判定为 `InstallationKind::Registered`；
- `compiledReleaseIdentity()` 可用；
- 下载快照为 `DownloadStatus::Verified` 且携带 `UpdateSelection`；
- `verifyPackageForExecution` 通过并取得 `VerifiedPackage` 租约。

`ApplicationUpdateHandoff::Request` 相应扩展，携带安装根与清单给出的期望大小和
SHA-256；`packagePath` 与 `releaseVersion` 字段维持现有语义。

界面改两处文案分支。现在 `updatecheckdialog.cpp` 的 `else` 兜底让"此版本尚未启用安装
更新"在"已是最新版"时也会显示，这正是本次问题的起因。改为：已是最新版时不提及安装能力；
执行能力确实关闭时才说明该构建不支持程序内安装。

## 错误处理

失败一律 fail-closed，不存在"部分安装成功"的用户可见状态：

| 失败点 | 行为 |
| --- | --- |
| 包校验不通过 | 安装按钮不出现；下载状态提示重新下载 |
| UAC 被拒绝 | 映射 `ApprovalDeclined`，会话不变，窗口恢复 |
| 安装器启动失败 | 映射 `LaunchFailed`，凭据文件清理，预留释放 |
| 引擎 prepare 失败 | 拒绝提交退出，GUI 保持运行，什么都没改 |
| 事务执行中断 | 引擎日志驱动反向回放，安装目录回到事务前状态 |
| 重启确认失败 | 报 `RestartFailed`，保留事务备份供授权恢复，用户手动启动 |

`ApplicationUpdateHandoff` 既有的世代号、预留释放与准备丢失处理不改动，其正确性已由
`tests/ui_acceptance/updatehandofftest.cpp` 覆盖。

## 测试与验收

**Linux CI 可自动化：**

- 包校验信任根翻转：空指纹列表下哈希锚定通过；篡改一字节判 `HashMismatch`；大小不符判
  `SizeMismatch`；指纹列表非空时仍走 Authenticode 分支。
- `makeInstalledRelease` 接线：官方构建加注册安装产出 `InstalledRelease`；三项输入各自
  缺失时产出 `std::nullopt`。
- bootstrap v4 编解码与跨版本拒绝：v3 数据喂给 v4 解析器必须拒绝。
- 会话工厂开闸条件矩阵：五个条件逐个取反，各自都必须导致空工厂。

**Windows CI（`update-smoke.yml`，windows-2022 runner）：**

在既有真实 Inno 安装加事务引擎端到端用例之外，新增一条协调者中继用例，覆盖
`AwaitingAppExit` 转发、`CommitExit` 转发与 Proceed 放行时序。

**真实环境验收（VM，人工）：**

按 [UPDATE_ACCEPTANCE_VM.md](../../development/UPDATE_ACCEPTANCE_VM.md) 执行，本次至少
补充：真实 UAC 提权与用户拒绝两条路径、真实 HKLM 写入与登记回滚、安装目录替换事务的
中断恢复、装完自动重启与启动确认。这些无法在 Linux 开发机上验证。

## 发布顺序

1. 本次改动合入 master，CI 通过。
2. 发布 v26.09.03，流水线携带 `ZZLOGG_OFFICIAL_RELEASE=ON`。该版本仍需手动安装，
   v26.09.02 不具备也无法被远端改造出这项能力。
3. v26.09.03 需要一个更新的版本才能触发安装，因此真正的端到端一键安装要到发布
   v26.09.04 时才跑得通。不等两个版本的话，用 `tools/update/testfeed.cpp` 的离线测试源
   在 VM 上以假清单提前验证整条链路；该步骤写入验收手册。

## 文档更新

- [GITHUB_UPDATES.md](../../development/GITHUB_UPDATES.md)：改写"目前没有 Windows 代码
  签名证书……本次没有放宽安装包校验"与结尾"Windows 一键安装是后续独立工作"两段，说明
  清单锚定信任根的裁决、UAC 未知发布者的已知限制与稳定版渠道范围。
- [UPDATE_PROTOCOL.md](../../development/UPDATE_PROTOCOL.md)：新增 bootstrap v4 字段
  说明与协调者生产中继时序；更新"3C 未交付项"一节，划掉本次交付的三项。
- [UPDATE_ACCEPTANCE_VM.md](../../development/UPDATE_ACCEPTANCE_VM.md)：补充测试源预演
  步骤与本次新增的验收用例。
- `CHANGELOG.md`：在 `## [Unreleased]` 下记录一键安装更新。
