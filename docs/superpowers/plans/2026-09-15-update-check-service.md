# 更新系统第二阶段 A：检查、通知与持久化实现计划

> **面向 AI 代理的工作者：** 使用 executing-plans 在当前任务内顺序实施，每项先写失败测试，验证后单独提交中文标题及详细中文说明。不得并行修改共享更新状态机，不自动合并或推送。

**目标：** 把已有签名验证核心接入 Qt 网络和应用界面，实现可取消、可解释、不会回退信任状态的更新检查。

**架构：** 无 Qt 的 update 核心保留原边界；新增 updateqt 静态库，承担网络、持久化和检查调度。KloggApp 持有唯一服务，主窗口和设置页只订阅状态、发送意图。

**技术栈：** C++17、Qt 6 Core/Network/Widgets/Test、ZzPureTools 现有主题、CMake。

**规格：** [应用更新设计](../specs/2026-09-15-application-update-design.md)，尤其第 5–7、12–13 节；[第一阶段协议](../../development/UPDATE_PROTOCOL.md)。

## 全局约束与本次交付边界

- 首期支持 Windows x64，沿用 Qt 6 主程序与 ZzPureTools 静态链接方式；不增加运行时 DLL。
- 本次只检查和通知，不下载发布包，不启动安装器、不重启应用、不改程序目录。
- 设计第二阶段拆为 A（本计划）和 B（下载与缓存验证）。B 完成后才向用户提供下载操作，不能放置看似可用的安装按钮。
- 没有生产地址或生产公钥时显示“尚未配置更新服务”；后台不请求、不弹错。不能把此状态显示为“已经是最新版本”。
- 不提供用户可编辑的更新 URL、关闭证书验证或切换测试信任环境的设置/命令行选项。
- 开发构建身份不根据 git tag 列表猜测。未提供发布序号和可信部署信息时保持开发/未知身份，能展示已验证发布信息，但不能报告可安装。
- 不修改用户日志、存储定位和数据迁移；现有 .arts、serach.png、serach.svg 不提交。
- Windows 自动化测试与深浅色、三种语言、150% 缩放检查；不要求运行其他操作系统。
- 从 master 的 13d63a7c 开始；复用 .worktrees/update-core，分支 codex/update-check。

## 已核对的接入位置

| 文件 | 现状与变更 |
| --- | --- |
| src/versioncheck/include/versionchecker.h、src/versioncheck/src/versionchecker.cpp | 旧无签名版本列表、七天截止时间；应用接入完成后删除旧实现，不保留双套调度 |
| src/app/kloggapp.h | 原 VersionChecker 成员、startBackgroundTasks、newVersionNotification；替换为唯一服务及非模态通知 |
| src/ui/src/mainwindow.cpp、mainwindowmenus.cpp、include/mainwindow.h | 新增“帮助 → 检查更新”动作和请求信号，覆盖动态翻译 |
| src/ui/src/optionsdialog.cpp、optionsdialogpages.cpp、include/optionsdialog.h、optionsdialog.ui | 将原版本检查复选框移到独立“更新”页；保持 Apply/Cancel 语义 |
| src/settings/include/configuration.h、src/settings/src/configuration.cpp | 复用已有自动检查布尔值，增加稳定/预览渠道，不复用旧截止时间 |
| cmake/ZzLoggBrand.cmake、cmake/zzlogg_brand.h.in | 空生产地址保持关闭；新增编译时配置工厂，不生成生产密钥 |
| src/update/src/payload.cpp | 提取并公开已有 URL 允许策略供网络复用，保持原协议测试 |

## 状态与安全约定

### 持久化

状态位于 StorageContext::current().runtimePaths().appConfigDirectory 下的 updates/production/check-state-v1.json；这是现有稳定用户配置根，不随日志数据根切换，不写入便携程序目录。不得从当前工作目录推导路径。

文件包含 schema=1 以及 stable、preview 两个独立记录：accepted（序号字符串和 128 个小写十六进制字符组成的 SHA-512 摘要）、lastSuccess、nextAttempt、failureCount、skippedReleaseSequence。无 accepted 时为 null，其他序号字段使用十进制字符串，不能经 double 中转。

读入上限 16 KiB，使用核心严格 JSON 解析辅助器，拒绝重复键、未知结构、非法渠道、负数时间、错误摘要和溢出。文件不存在是首次运行；已存在但损坏是 StateInvalid，不静默重置或覆盖。诊断不记录清单全文或用户路径。

每次修改使用 QLockFile 非阻塞锁与 QSaveFile（禁止 directWriteFallback）。锁内重新读取最新文件，仅修改目标渠道字段，保留另一渠道；保存失败保留旧文件。锁失败报告 StateBusy，不长时间阻塞 UI。

网络请求期间不持锁。完整验签完成后，accept 再次在锁内与最新 accepted 比较：低序号拒绝、同序号不同摘要拒绝，防止两个进程先后覆盖回旧状态。成功持久化后才发布检查成功/新版结果。合法但不匹配平台的清单也保存 accepted。

### 调度

默认每天后台检查一次，应用运行期间每 60 秒评估到期时间，启动后延迟 10 秒首次评估。成功后 nextAttempt=now+86400，失败依次退避 15 分钟、1 小时、6 小时、24 小时，之后保持 24 小时。

手动检查绕过周期、自动检查开关和跳过版本，但复用正在进行的同渠道请求；不创建重复请求。手动请求附着后台请求后，完成结果必须显示给用户。取消不增加失败计数、不推进 accepted，并把后台下一次检查延后 15 分钟，避免取消后马上重试。

未配置不增加失败计数；本地损坏/锁忙不会被写操作“修复”。过去时间使用饱和加法，nextAttempt 超过 now+86400 的异常未来值不能无限延后检查；验证器仍决定时钟是否有效，不能绕过 ClockInvalid。

“跳过此版本”仅抑制该渠道相同 releaseSequence 的后台通知，不抑制验签和 accepted 保存；下一发布序号重新通知。“稍后提醒”只关闭当前提示，下个正常检查周期允许再次通知。跳过值只在用户点击后写入。

### 网络

独立 QNetworkAccessManager，使用系统代理和默认系统证书信任，不与日志下载器共享配置、Cookie 或认证缓存。设置 ManualRedirectPolicy；只允许 301/302/303/307/308 重定向，最多 3 次。每次请求前验证 URL，最终 reply URL 再次验证。

沿用核心精确主机规则；拒绝 HTTPS 降级、用户信息、片段、非法转义及非允许域名。对 Location 原文先检查长度（8192 字节）、ASCII 和禁用字符，再解析相对地址，禁止在规范化之后掩盖非法输入。

整次检查总超时 30 秒，另设 10 秒无传输超时；重定向不能重置总计时。证书错误立即失败，任何路径都不调用 ignoreSslErrors。仅最终 HTTP 200 可作为清单，304 无本地已验证正文缓存时也是失败。

请求 Accept-Encoding: identity，拒绝非 identity 响应编码；元数据到达时检查 Content-Length，接收时仍独立计数，最多 256 KiB。设置有限 reply 读缓冲，分块读取，超过上限即 abort；不能无界 readAll 后再检查。

使用递增请求 generation 与 QPointer<QNetworkReply>；取消、换渠道、销毁服务后，旧回调不得写状态或更新窗口；终态信号每次请求只发一次。

## 公开接口约定

新增 namespace zzlogg::updateqt，不把 UI 类放进核心：

```cpp
enum class Channel { Stable, Preview };
enum class CheckOrigin { Manual, Background };
enum class CheckStatus {
    Idle, NotConfigured, Checking, Cancelled, UpToDate, Available,
    ReleaseInformation, Unsupported, NetworkError, VerificationFailed,
    StateInvalid, StateBusy, StateWriteFailed
};
struct ChannelState {
    std::optional<zzlogg::update::AcceptedMetadata> accepted;
    qint64 lastSuccess = 0, nextAttempt = 0;
    unsigned failureCount = 0;
    std::optional<std::uint64_t> skippedReleaseSequence;
};
enum class StateError { None, Invalid, Busy, ReadFailed, WriteFailed, Replay, Conflict };
struct StateResult { std::optional<ChannelState> value; StateError error; };
class UpdateStateStore {
public:
    explicit UpdateStateStore(QString absoluteFile);
    StateResult read(Channel) const;
    StateError accept(Channel, const zzlogg::update::AcceptedMetadata&, qint64 now);
    StateError recordFailure(Channel, qint64 now);
    StateError recordCancellation(Channel, qint64 now);
    StateError skip(Channel, std::uint64_t releaseSequence);
};
struct FeedConfiguration {
    QString stableUrl, previewUrl;
    std::vector<zzlogg::update::TrustedKey> keys;
    std::vector<std::string> allowedHosts;
    qint64 buildTime = 0;
    // Production in application factory; Test only in test-owned construction.
    zzlogg::update::TrustEnvironment environment = zzlogg::update::TrustEnvironment::Production;
};
struct CheckSnapshot {
    CheckStatus status = CheckStatus::Idle;
    Channel channel = Channel::Stable;
    std::optional<zzlogg::update::VerifiedManifest> release;
    std::optional<zzlogg::update::Decision> decision;
    bool presentToUser = false;
};
```

UpdateService 为 QObject，构造参数为 FeedConfiguration、UpdateStateStore 的共享所有权、std::optional<InstalledRelease>、注入时钟 std::function<qint64()> 和父对象；内部拥有 ManifestFetcher。

服务提供 requestCheck(Channel, CheckOrigin)、cancel()、setAutomaticChecking(bool)、setChannel(Channel)、skipCurrentRelease() 和 snapshot()；发出 snapshotChanged()，接收者再读快照。测试用工厂注入网络管理器，仅替换外部传输边界，不模拟验证器。

ManifestFetcher 提供 start(QUrl, allowedHosts)、cancel() 和 succeeded(QByteArray)/failed(FetchError) 信号；FetchError 定义 InvalidUrl、Tls、Http、Network、TooLarge、Timeout、Redirect、Cancelled。应用不会直接读取未验证正文。

## 任务 1：原子渠道状态与调度规则

**创建：** src/updateqt/CMakeLists.txt、include/zzlogg/updateqt/updatestate.h、src/updatestate.cpp、tests/updateqt/CMakeLists.txt、tests/updateqt/updatestatetest.cpp。
**修改：** src/CMakeLists.txt（update 后加入 updateqt）、根 CMakeLists.txt（测试注册）。

- [x] 建立新静态库，只链接 Qt Core 和 update 核心；状态实现可私有链接现有 JSON 目标，不能将 JSON 类型暴露给 UI。临时状态均使用 QTemporaryDir。
- [x] 先实现返回 WriteFailed 的可编译骨架，写读回与反回退测试并观察失败：

```cpp
QTemporaryDir directory;
UpdateStateStore store(directory.filePath("state.json"));
AcceptedMetadata newer{2, {}};
newer.payloadDigest[0] = 0x42;
QCOMPARE(store.accept(Channel::Stable, newer, 1800000000), StateError::None);
auto state = store.read(Channel::Stable);
QVERIFY(state.value && state.value->accepted);
QCOMPARE(state.value->accepted->sequence, std::uint64_t(2));
QCOMPARE(state.value->nextAttempt, qint64(1800086400));
QCOMPARE(store.accept(Channel::Stable, AcceptedMetadata{1, {}}, 1800000001),
         StateError::Replay);
QVERIFY(!store.read(Channel::Preview).value->accepted);
```

- [x] 实现严格读取、锁内重读与原子替换；写入失败测试让目的路径为目录，确认旧 accepted 不变；持有同名锁时第二实例返回 Busy。两个 store 依次写 3、2，确认第二次拒绝。
- [x] 覆盖摘要冲突、uint64 最大值、两渠道交错更新、文件截断/重复字段/超限、失败退避序列、取消退避、时间溢出与跳过值保存。读失败不能返回默认有效状态。
- [x] 注册 zzlogg_update.state，运行构建及 CTest 后提交“feat: 持久化更新渠道状态与检查周期”。

任务 1 验证记录：可编译空实现出现 7 个预期失败；实现后状态测试 9 项通过，完整 CTest 71/71 通过。Windows 使用禁止删除共享的真实文件句柄验证原子替换失败不破坏旧文件；只读审查无重要问题，并补充有效 JSON 的 16 KiB 精确边界测试。

## 任务 2：有界 HTTPS 清单获取

**创建：** src/update/include/zzlogg/update/urlpolicy.h、src/update/src/urlpolicy.cpp、src/updateqt/include/zzlogg/updateqt/manifestfetcher.h、src/updateqt/src/manifestfetcher.cpp、tests/updateqt/manifestfetchertest.cpp、tests/updateqt/scriptednetwork.h。
**修改：** src/update/src/payload.cpp、两个核心 CMakeLists、tests/updateqt/CMakeLists.txt。

- [x] 将现有 allowedUrl 原样提取为 bool isAllowedUpdateUrl(std::string_view, const std::vector<std::string>&)，核心与网络共用。先运行已有 policy URL 测试，保持全部规则。
- [x] ScriptedNetworkManager 继承 QNetworkAccessManager，只覆写 createRequest 返回按脚本分块发信号的 QNetworkReply；记录请求 URL、重定向策略及读取量。它不负责判断是否安全，判定由真实 ManifestFetcher 完成。
- [x] 先用返回成功的可编译骨架，运行 HTTP 降级必须失败的测试，确认缺失验证被抓住：

```cpp
// test-owned network manager maps the first HTTPS request to a 302 with an HTTP Location.
QSignalSpy rejected(&fetcher, &ManifestFetcher::failed);
fetcher.start(QUrl("https://updates.example.invalid/manifest"),
              {"updates.example.invalid"});
QTRY_COMPARE(rejected.count(), 1);
QCOMPARE(network.requestedUrls().size(), 1); // Never sends the HTTP request.
```

- [x] 实现手动重定向、最终 URL 校验、状态码、证书失败、块读取限额和总超时；取消 disconnect/abort/deleteLater，旧 generation 回调直接返回。
- [x] 测试无 Content-Length 逐块超限、虚假长度、空响应、压缩响应、3/4 次重定向边界、相对 Location、同后缀恶意主机、TLS 错误、慢速连续小块不逃逸总超时、取消后迟到 finished。超时值通过内部测试构造参数缩短，生产值固定。
- [x] 增加回环 QTcpServer 的真实 HTTP 拒绝用例，不能仅靠替身证明“不发 HTTP”；没有生产 HTTPS 服务时，不声称完成真实服务端互通验证。
- [x] 注册 zzlogg_update.fetcher，原核心及获取测试全绿后提交“feat: 安全获取签名更新清单”。

任务 2 验证记录：拒绝降级先观察预期失败；获取器覆盖大小、状态码、重定向、超时、取消及销毁。审查发现 finished 收尾在错误回调中重入新请求的问题，增加失败回归后用对象存活和 generation 检查修复。真实回环 TCP 服务确认 HTTP 请求不会连接；生产 HTTPS 互通未执行。Qt 会先去除 HTTP 头值首尾合法空白，因此原文检查针对 Qt 提供的头值、在 QUrl 解析前执行，URL 内部空白仍拒绝；已有专门测试记录该边界。

## 任务 3：应用级检查服务与生产配置关闭边界

**创建：** src/updateqt/include/zzlogg/updateqt/updateservice.h、src/updateqt/src/updateservice.cpp、src/updateqt/src/updateconfiguration.cpp、tests/updateqt/updateservicetest.cpp。
**修改：** cmake/ZzLoggBrand.cmake、cmake/zzlogg_brand.h.in、src/updateqt/CMakeLists.txt、tests/updateqt/CMakeLists.txt。

- [x] 工厂读取编译时生产配置；地址、公钥表、允许主机任一缺失时返回未配置。默认保持空地址/空公钥。新增 CMake CACHE STRING：ZZLOGG_UPDATE_STABLE_URL、ZZLOGG_UPDATE_PREVIEW_URL、ZZLOGG_UPDATE_PUBLIC_KEYS（分号分隔的 keyId:64位小写公钥十六进制）、ZZLOGG_UPDATE_ALLOWED_HOSTS（分号分隔的小写 DNS 名）；校验 keyId 与域名格式、重复 ID、长度和转义，禁止将未经转义的配置拼进 C++。由 configure_file 生成内部配置头，构建时间用 UTC Unix 秒。旧 ZZLOGG_UPDATE_MANIFEST_URL 在删除旧版本检查器时移除。不从用户设置读取密钥或 URL。
- [x] 使用 tests/update/fixturehelper.h 的真实已签名负载，经 scripted network 进入真实 fetcher、verifyManifest、state store；用固定时钟验证服务。先以未接 verifier 的实现观察测试失败。
- [x] 测试完整路径：requestCheck(Stable, Manual) → Checking → 网络返回 → 验签 → accepted 落盘 → Available 或 ReleaseInformation；存储成功之前不发成功快照。

```cpp
service.requestCheck(Channel::Stable, CheckOrigin::Manual);
QTRY_COMPARE(service.snapshot().status, CheckStatus::ReleaseInformation);
QVERIFY(service.snapshot().release);
QVERIFY(store->read(Channel::Stable).value->accepted);
// Installed identity is absent in this fixture, so installation must not be inferred.
QVERIFY(!service.snapshot().decision);
```

- [x] 正式 InstalledRelease 仅由明确发布信息构造；默认身份缺失时展示 ReleaseInformation，不把用户选择的数据根映射为 Portable/Installer，也不伪造 releaseSequence。
- [x] 实现手动请求附着后台检查、每日检查、失败退避、跳过后台通知。轮询定时器读取时钟与最新状态；受测纯调度函数与定时器调用使用同一实现。
- [x] 状态错误拒绝成功；检查期间用户换渠道时取消旧请求，旧结果不能写入新渠道。多实例竞态测试在响应前让另一 store 保存更大序号，确认不能回退或通知旧结果。
- [x] 覆盖无配置零请求、开发身份、无匹配载荷仍推进 accepted、错误签名不推进、重复点击一请求、后台请求转手动、取消与销毁服务后零回调。
- [x] 注册 zzlogg_update.service，全绿后提交“feat: 连接签名验证与应用更新检查服务”。

任务 3 验证记录：检查服务先观察未接验签骨架的预期失败，再经真实签名清单、传输脚本和临时状态文件完成闭环。服务测试 19 项通过，更新相关 CTest 10/10 通过。审查发现换渠道残留快照、取消未保存退避，均先补失败回归再修复，并通过复审。生产配置写入独立内部头，所有外部字符串先编码为十六进制，配置契约验证重复密钥、长度、非法域名及 C++ 注入边界；应用界面与主程序接线仍属于任务 4–5。

## 任务 4：检查窗口、设置页与三语言通知

**创建：** src/ui/include/updatecheckdialog.h、src/ui/src/updatecheckdialog.cpp、src/ui/include/updatesettingspage.h、src/ui/src/updatesettingspage.cpp、tests/ui_acceptance/updatecheckuitest.cpp。
**修改：** mainwindow.h/mainwindow.cpp/mainwindowmenus.cpp、mainwindowtext.h/.cpp、optionsdialog.h/.cpp、optionsdialogpages.cpp、optionsdialog.ui、configuration.h/.cpp、src/ui/CMakeLists.txt、src/app/i18n/en.ts、src/app/i18n/zh_CN.ts、src/app/i18n/zh_TW.ts、tests/ui_acceptance/CMakeLists.txt。

- [x] 先写对象行为测试：单次菜单激活只发一个检查信号；未配置状态不是最新状态；唯一窗口由任务 5 的应用所有者创建，重复打开的集成测试随该任务执行。使用真实控件，服务测试数据通过 CheckSnapshot 输入，不发真实网络。
- [x] UpdateCheckDialog 为非模态 QDialog，内容滚动、底部操作区固定；尺寸按当前屏幕 availableGeometry 限制，沿用现有主题与动态 LanguageChange。更新说明强制 PlainText，不启用外部链接。
- [x] 窗口显示当前版本、渠道、检查状态与时间、已验证新版本和所选语言说明；只有具备明确发布身份及选中载荷时展示体积/类型。当前阶段显式提示下载/安装尚未接入。
- [x] 操作为“检查/重试、取消检查、稍后、跳过此版本、关闭”；根据状态启用；关闭活动窗口视为取消当前手动检查，不退出主程序。后台新版只展示非模态提示，不强抢焦点。
- [x] 独立“更新”设置页复用自动检查设置并提供稳定/预览渠道和立即检查入口。编辑开关/渠道只有 Apply/OK 后生效，Cancel 不保存；立即检查使用已保存渠道，界面明确显示此渠道，避免未应用草稿偷偷生效。
- [x] 所有文本支持 en、zh_CN、zh_TW 实时切换；已有设置页布局与样式不重构。测试三语言超长说明、深浅色及 1280×800 逻辑区域内底部按钮可见，并实际运行 Windows 原生 150% Qt 缩放。
- [x] 注册 zzlogg_ui.update_check，验证设置 Apply/Cancel、菜单与语言行为后提交“feat: 新增检查更新窗口与更新设置页”。

任务 4 验证记录：窗口骨架先出现 3 项失败，菜单/设置入口再出现 2 项缺失失败；实现后 UI 11 项通过，更新相关与界面 CTest 11/11 通过，已有翻译、行号和设置主题回归 4/4 通过。Windows 原生 QT_SCALE_FACTOR=1.5 实测三语言双主题长说明，900×720 像素截图中的底栏完整可见（600×480 逻辑尺寸）。审查修复检查时间被展示操作改写的问题，时间改由服务快照提供；无配置不伪造检查时间。

## 任务 5：应用接线、移除旧路径与整体验收

**修改：** src/app/kloggapp.h、src/app/CMakeLists.txt、src/CMakeLists.txt、src/ui/CMakeLists.txt、tests/ui_acceptance/CMakeLists.txt、docs/development/UPDATE_PROTOCOL.md。
**删除：** src/versioncheck/include/versionchecker.h、src/versioncheck/src/versionchecker.cpp、src/versioncheck/CMakeLists.txt。
**创建：** tests/updateqt/updatecheckintegrationsmoke.cpp。

- [x] 先写集成测试，证明“帮助 → 检查更新”经过应用对象到达唯一 UpdateService；设置变更能改变同一个服务，不另建后台检查器。
- [x] KloggApp 在 StorageContext 安装及配置初始化完成后构造服务，不在 QApplication 构造函数中过早读取存储。每个主窗口转发请求到同一对象，主实例才启动后台轮询；关闭/退出安全取消。
- [x] 替换 startBackgroundTasks 与旧 HTML newVersionNotification；移除旧服务及全部目标链接，保留原自动检查布尔值。旧截止时间键不再读取，不为兼容旧未签名协议保留解析器。
- [x] 测试里强制 Production 未配置时启动应用不发网络、不阻塞日志功能；测试信任只存在独立测试可执行文件。测试目标不加入 runtime。
- [x] 完整构建与回归：

```powershell
cmake -S . -B out/ui-vs -DKLOGG_BUILD_UI_TESTS=ON -DZZLOGG_BUILD_UPDATE_TEST_TOOLS=ON
cmake --build out/ui-vs --config Release --parallel 8
ctest --test-dir out/ui-vs -C Release --output-on-failure
```

- [x] 核对原 70 项以及新增测试逐项结果；既有 storage_migrator 的诊断日志仍保留。重新构建 runtime，确认无新增框架 DLL、无 fixture 或测试工具。
- [x] 只读审查网络重定向、生命周期、锁内更新和 UI 状态，修复重要问题；记录未执行的真实生产 HTTPS 互通测试。提交“feat: 接入主程序并完成更新检查验收”。

任务 5 验证记录：应用集成测试先出现服务数量 0、预期 1 的失败；接线后验证两主窗口共用一个服务/检查窗口及设置渠道生效。审查发现父窗口销毁绕过关闭事件，先补取消信号计数失败，再修复析构顺序并验证真实服务忽略迟到网络完成。只读复审无重要遗留。Release 完整构建成功，最终 CTest 76/76 通过（49.35 秒），UI 内部 13 项通过。runtime 重新生成成功，无测试工具、fixture 或框架 DLL；构建目录中原有部署产物已重建，未改动主工作区运行目录。生产 HTTPS 互通未执行，默认生产配置仍关闭。保留 codex/update-check 分支等待单独集成决定。

## 计划自检与下一步

检查/通知/每日周期/失败退避/渠道/跳过/取消对应任务 1、3、4；HTTPS/证书/有界正文对应任务 2；防回放持久化与竞态对应任务 1、3；三语言及缩放对应任务 4；真实应用接线和回归对应任务 5。

生产公钥、托管域名、部署描述与正式发布序号未由用户提供。它们不阻塞隔离测试及关闭状态 UI，但生产功能必须保持关闭，不能伪造配置“跑通”。

本计划完成后进入第二阶段 B：包下载进度、取消、专用缓存、最终地址及 SHA-256 校验。再后才是便携助手和安装器事务；本计划不承诺自动安装已经可用。

## 参考

- [QNetworkRequest](https://doc.qt.io/qt-6/qnetworkrequest.html)：手动重定向及传输超时；总时限需独立控制。
- [QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)：提交替换与禁止退回原地写入。
- [QLockFile](https://doc.qt.io/qt-6/qlockfile.html)：跨进程协作锁，锁不是抵御恶意本地用户的授权边界。
