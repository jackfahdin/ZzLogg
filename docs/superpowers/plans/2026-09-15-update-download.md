# 更新包下载与校验实现计划（第二阶段 B）

> **面向 AI 代理的工作者：** 必需子技能：使用 executing-plans 按依赖顺序实现此计划。步骤使用复选框跟踪进度。沿用用户已批准的总体设计，不重复询问执行方式；每个可测试交付物使用中文标题与详细中文说明提交。

**目标：** 用户主动下载已验签清单中选中的全量包，展示进度，支持取消和重试，只有长度与 SHA-256 同时正确才输出已验证缓存文件。

**架构：** 独立缓存写入器负责有界流式落盘和哈希，下载器负责 HTTPS 与响应生命周期，应用级协调器负责从真实检查结果创建下载请求。UI 只发送用户意图、显示快照，不接收任意 URL 或文件路径作为可信下载任务。

**技术栈：** C++17、Qt 6 Core/Network/Widgets、QSaveFile、QCryptographicHash、QLockFile、现有静态 zzlogg_update_qt 与 ZzPureTools。

**规格：** [应用更新系统设计](../specs/2026-09-15-application-update-design.md)，重点为第 3、6、7、8、12、13 节；已完成的 [检查服务计划](2026-09-15-update-check-service.md) 是前置交付物。

## 全局约束

- 首期支持 Windows x64；保留其他平台编译结构，不要求本轮实际跨平台测试。
- 首期仅全量包，不实现差分、强制更新、常驻服务或跨平台文件替换。
- 不实现解压、安装、提权、重启替换、文件事务及断点续传；不显示可执行的“重启安装”按钮。
- 包长度沿用协议上限 512 MiB；不提高现有清单验证器的限制。
- 生产地址、公钥和可信发布身份仍缺失。默认程序保持未配置，不能编造正式身份或把测试信任放进应用。
- 后台检查绝不自动下载；必须来自用户的下载操作。
- 缓存根由 QStandardPaths::CacheLocation 加固定 updates/production/packages-v1 后缀提供，不从日志路径、StorageContext 数据根、HTTP 文件名或命令行 URL 推导。
- 根路径为空、不绝对、不可写、含符号链接或 Windows 重解析点时拒绝；不退回当前目录或程序目录。
- 不自动清理用户目录，不递归删除缓存根。只删除本次对象拥有的临时文件；未知文件保留。
- SHA-256 通过成熟 Qt 实现计算；网络输入不整包读入内存，不继承日志下载的忽略 SSL 错误开关。
- 每个任务先观察针对真实行为的失败，再实现并验证；只替换网络、时钟与文件系统边界，不伪造验签成功结果。
- 不推送、不创建发布、不生成生产密钥、不部署在线服务器。

## 文件与职责

| 文件 | 职责 |
| --- | --- |
| src/updateqt/include/zzlogg/updateqt/packagecache.h、src/updateqt/src/packagecache.cpp | 单包流式写入、大小/哈希校验、缓存锁及失败清理 |
| src/updateqt/include/zzlogg/updateqt/packagedownloader.h、src/updateqt/src/packagedownloader.cpp | 有界网络流、重定向、超时、进度及取消 |
| src/updateqt/include/zzlogg/updateqt/updatedownloadservice.h、src/updateqt/src/updatedownloadservice.cpp | 可信检查结果到下载任务的门禁与快照 |
| src/updateqt/include/zzlogg/updateqt/updatecachepaths.h、src/updateqt/src/updatecachepaths.cpp | 系统缓存路径解析，与用户数据目录解耦 |
| src/ui/include/updatecheckdialog.h、src/ui/src/updatecheckdialog.cpp | 下载操作、进度、重试与三语言展示 |
| src/app/kloggapp.h | 应用级唯一下载服务、检查/渠道/退出生命周期接线 |
| tests/updateqt/packagecachetest.cpp、packagedownloadertest.cpp、updatedownloadservicetest.cpp | 缓存、网络与业务门禁测试 |
| tests/ui_acceptance/updatecheckuitest.cpp、tests/updateqt/updatecheckintegrationsmoke.cpp | UI 与真实应用入口回归 |

## 任务 1：可验证的流式缓存写入器

**修改：** src/updateqt/CMakeLists.txt、tests/updateqt/CMakeLists.txt。创建上表中的 packagecache 文件及 packagecachetest.cpp。

- [x] 定义 CacheError：None、InvalidPath、Busy、InvalidArtifact、InsufficientSpace、WriteFailed、SizeMismatch、HashMismatch、Cancelled。定义 PackageCache，构造参数为绝对缓存根、Artifact；公开 begin()、append(QByteArrayView)、finish()、cancel()、verifiedPath()。返回 CacheError；verifiedPath 在成功前必须为空。Artifact 在此仅提供长度与哈希，不能构成业务下载授权。
- [x] 先实现返回失败的可编译骨架，用真实 QTemporaryDir 和 SHA-256 写最小失败测试；不是检查源码文本。

```cpp
QTemporaryDir root;
const QByteArray bytes("verified package");
zzlogg::update::Artifact artifact;
artifact.size=bytes.size();
artifact.sha256=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
PackageCache cache(root.path(),artifact);
QCOMPARE(cache.begin(),CacheError::None);
QVERIFY(cache.verifiedPath().isEmpty());
QCOMPARE(cache.append(bytes.first(4)),CacheError::None);
QCOMPARE(cache.append(bytes.sliced(4)),CacheError::None);
QCOMPARE(cache.finish(),CacheError::None);
QFile saved(cache.verifiedPath());
QVERIFY(saved.open(QIODevice::ReadOnly));
QCOMPARE(saved.readAll(),bytes);
```

- [x] 使用固定 SHA-256 文件名 `<sha256>.package` 和同名 QLockFile；文件名不取 URL 或 Content-Disposition。持锁至提交或取消。使用 QSaveFile，显式关闭 directWriteFallback，未验证前不 commit。
- [x] begin 检查长度 1..512 MiB、64 位小写十六进制哈希、根及已有祖先路径。Windows 用 GetFileAttributesW 拒绝 FILE_ATTRIBUTE_REPARSE_POINT；其他平台检查符号链接。路径检查不声称能抵抗完全控制当前用户账户的攻击者。
- [x] QStorageInfo 检查至少包长度加 16 MiB 余量；无法获知空间则返回错误。写入时仍检查短写/磁盘满；空间预检不是成功保证。通过小型存储空间查询接口测试不足分支，真实磁盘读写仍由 Qt 完成。
- [x] append 在写入前检查累计长度，超限立即终止；每个块同时进入 QCryptographicHash(Sha256)。finish 检查精确长度、哈希后才 commit，并检查提交结果。错误和取消清空可用路径，销毁未提交文件、释放锁。
- [x] 已有同名文件不作为可信缓存直接使用；本期重新下载、验证后原子替换。失败不能损坏原来的文件。取消和析构重复调用幂等；已有未知文件及邻近目录完全不动。
- [x] 增加数据驱动用例：零长度、上限外长度、非法哈希、空/相对根、根为文件、重解析点、锁竞争、短包、长包、错哈希、写失败、取消、未完成析构、成功路径、旧文件保护。测试预留的故障接口只抽象实际文件系统职责，不添加生产测试模式。
- [x] 注册 zzlogg_update.package_cache，运行命令并提交“feat: 新增更新包流式缓存与哈希校验（任务 1/5）”。

```powershell
cmake --build out/ui-vs --config Release --target zzlogg_update_package_cache_test --parallel 8
ctest --test-dir out/ui-vs -C Release -R '^zzlogg_update.package_cache$' --output-on-failure
```

## 任务 2：HTTPS 包下载器

**创建：** packagedownloader.h/.cpp、packagedownloadertest.cpp。修改两个 CMakeLists.txt；仅必要时扩展 tests/updateqt/scriptednetwork.h。

- [x] 定义 PackageDownloader(QObject)：start(Artifact, allowedHosts, cacheRoot)、cancel()；信号 progress(qint64 received,qint64 total)、succeeded(QString verifiedPath)、failed(DownloadError)。DownloadError 包含缓存错误以及 InvalidUrl、Http、Tls、Redirect、Timeout、Network、Cancelled；保持可区分的类型映射，不把哈希错误当网络错误。
- [x] 使用真实已签名 fixture 中的包哈希与脚本网络流，先验证目前不存在成功落盘行为。测试 helper 修改 fixture 内容后必须重新签名并经过 verifyManifest，不能手造 VerifiedManifest。

```cpp
QSignalSpy success(&downloader,&PackageDownloader::succeeded);
QSignalSpy failure(&downloader,&PackageDownloader::failed);
downloader.start(artifact,context.allowedHosts,root.path());
QTRY_COMPARE_WITH_TIMEOUT(success.count(),1,2000);
QCOMPARE(failure.count(),0);
QFile package(success.at(0).at(0).toString());
QVERIFY(package.open(QIODevice::ReadOnly));
QCOMPARE(package.readAll(),expectedBytes);
```

- [x] 使用独立 QNetworkAccessManager，手动重定向，禁用自动 Cookie/认证复用/缓存响应，Accept-Encoding: identity。禁止 ignoreSslErrors。逐次验证原始 Location 和解析后的 HTTPS URL，最多 3 次重定向；校验最终 reply URL 同样属于允许主机。
- [x] 只接受最终 HTTP 200，不接受 206/304、压缩编码、非法 Content-Length。声明长度存在时必须等于已签名长度，缺失时按实际流长度判定。重定向响应不写入包文件，限制其响应体读取量至 64 KiB。
- [x] 单次读取不超过 64 KiB，reply 缓冲限制 128 KiB；所有路径包括 finished-only 都走相同 consume。每次事件最多处理 1 MiB，余量安排队列继续消费，防止持续 readyRead 饿死界面。以实际成功写入字节发布进度，不信任 HTTP 下载进度作为验证依据。
- [x] 默认无传输超时 30 秒，整次下载总时限 30 分钟；测试构造可注入更短期限。取消、销毁、新请求替换使用 generation 和断开旧 reply，迟到事件不能写入或重复发终态。发信号之后的代码通过 QPointer 防止同步销毁/restart 重入。
- [x] 测试 HTTPS 降级、跨主机/恶意 Location、超跳、TLS、HTTP 错误、压缩、短长包、错哈希、未知长度、超时、限额读、finished-only、重复 start、取消、信号内销毁、重入与迟到回调。证明坏包不会产生 verifiedPath。
- [x] 注册 zzlogg_update.package_download，运行该测试及已有 fetcher 测试，提交“feat: 实现更新包安全下载与取消（任务 2/5）”。

```powershell
cmake --build out/ui-vs --config Release --target zzlogg_update_package_download_test --parallel 8
ctest --test-dir out/ui-vs -C Release -R '^zzlogg_update.(package_download|fetcher|package_cache)$' --output-on-failure
```

## 任务 3：可信下载任务与应用级协调

**创建：** updatedownloadservice.h/.cpp、updatecachepaths.h/.cpp、updatedownloadservicetest.cpp。修改 UpdateService 只限必要的明确接口，不重构无关检查逻辑。

- [x] 定义 DownloadStatus：Idle、Downloading、Verified、Cancelled、Failed、Unavailable；DownloadSnapshot 包含状态、received/total、错误与 verifiedPath，失败时路径为空。
- [x] 定义 UpdateDownloadService：构造时注入 FeedConfiguration、可选 InstalledRelease、缓存根、时钟、网络工厂；公开 requestDownload(const CheckSnapshot&)、cancel()、invalidate()、snapshot()；发 snapshotChanged。它自行用 selectUpdate 重新计算决定，不能只相信快照中的公开 decision 字段。
- [x] requestDownload 必须要求 Available 快照、VerifiedManifest、明确非开发发布身份，当前时间仍在签发/过期范围，渠道一致；从重新选择出的 Artifact 取得 URL、长度、哈希。缺身份、仅 ReleaseInformation、NotConfigured、Expired、渠道不符均零网络且零缓存写入。

```cpp
service.requestDownload(unconfiguredSnapshot);
QCOMPARE(service.snapshot().status,DownloadStatus::Unavailable);
QVERIFY(network->requests.isEmpty());
QVERIFY(!QFileInfo::exists(cachePath));
```

- [x] 正常请求在每次下载开始前检查可用生产配置；Test 配置只能由测试进程注入。相同活动请求合并，不启动第二个下载；不同版本/渠道需要 invalidate 取消旧流后重新确认，不自动跟随新清单下载。
- [x] 下载期间保留本次可信清单及产物身份。成功时再次检查有效期和任务代次；过期/换渠道后不发布 Verified。保存 verifiedPath 不表示可以执行，安装阶段必须再验签验包。
- [x] 缓存路径工厂只接受系统 CacheLocation，明确追加环境子目录；测试用 QStandardPaths::setTestModeEnabled(true) 与临时根，默认生产服务不能使用 fixture 环境路径。
- [x] 测试真实验签→策略→下载→哈希→Verified 全链路；覆盖伪造 decision、开发身份、过期前后、取消、版本变更、重复请求、错误重试及销毁。锁忙和空间错误应显示明确状态，不能被解释成已下载。
- [x] 注册 zzlogg_update.download_service，运行全部更新测试，提交“feat: 连接可信发布选择与下载协调服务（任务 3/5）”。

```powershell
cmake --build out/ui-vs --config Release --target zzlogg_update_download_service_test --parallel 8
ctest --test-dir out/ui-vs -C Release -R '^zzlogg_update\.' --output-on-failure
```

## 任务 4：下载进度界面和生命周期接线

**修改：** updatecheckdialog.h/.cpp、kloggapp.h、tests/ui_acceptance/updatecheckuitest.cpp、tests/updateqt/updatecheckintegrationsmoke.cpp、src/app/i18n/{en,zh_CN,zh_TW}.ts。

- [x] 增加 setDownloadSnapshot(const DownloadSnapshot&)、downloadRequested 信号、QProgressBar 和下载按钮；先测试 Available 且身份可信时可下载、NotConfigured/ReleaseInformation 不提供可执行下载。保留内容滚动和固定底栏。

```cpp
dialog.setDownloadSnapshot({DownloadStatus::Downloading,25,100});
auto* progress=dialog.findChild<QProgressBar*>("updateDownloadProgress");
QVERIFY(progress);
QCOMPARE(progress->value(),25);
QVERIFY(!dialog.findChild<QPushButton*>("updateDownload")->isEnabled());
```

- [x] 显示实际字节/总大小与百分比；完成状态文案为“下载已验证，安装功能尚未接入”。不调用 ShellExecute/QDesktopServices 启动下载包，不显示重启按钮。
- [x] 应用持有唯一下载服务；只响应明确点击。后台发现新版仅提示。检查新版本/切换渠道前 invalidate 下载；关闭窗口、父窗口销毁和应用退出均安全取消活动下载。已验证缓存可留磁盘，但下次操作不凭路径或存在标志跳过验证。
- [x] 错误可重试；重试复用当前可信检查结果并重新检查期限，不使用任意 UI 输入。下载中不允许跳过/切换到其他包而继续旧下载。
- [x] 三语言即时切换、深浅色、长说明、150% 缩放验证；下载/取消状态下底栏完整。应用测试默认仍未配置，不能为演示在生产入口加入测试公钥。
- [x] 更新 UI 文案说明本阶段支持下载但不支持安装；运行 UI、更新和旧翻译/设置测试，提交“feat: 新增更新包下载进度与操作界面（任务 4/5）”。

```powershell
cmake --build out/ui-vs --config Release --parallel 8
ctest --test-dir out/ui-vs -C Release -R 'zzlogg_update|update_check|translation|settings_theme' --output-on-failure
```

## 任务 5：整体验收与文档

- [x] 只读审查缓存路径、流读取边界、TLS/重定向、签名选择、有效期及 UI 生命周期；重要问题先补失败测试再修复。
- [x] 完整构建并运行全部 CTest，记录实际总数，不沿用旧阶段的 76 项数字冒充本阶段结果。检查无生产 fixture 信任、无自动下载、无执行包或修改程序文件路径。

```powershell
cmake --build out/ui-vs --config Release --parallel 8
ctest --test-dir out/ui-vs -C Release --output-on-failure
cmake --build out/ui-vs --config Release --target zzlogg_runtime_folder --parallel 8
```

- [x] runtime 重建前检查解析后的绝对目录严格在本工作树 out/ui-vs/runtime/Release 内，确认没有用户数据；部署后检查无测试程序、测试私钥或新增 ZzPureTools DLL。保留根工作区用户文件。
- [x] 更新 docs/development/UPDATE_PROTOCOL.md：列出已经实现的下载/校验、缓存位置与失败行为，明确未实现安装及真实生产互通。用户未提供的生产密钥/发布身份不能伪造。
- [x] 提交“test: 完成更新下载阶段安全验收（任务 5/5）”，报告程序绝对路径与验证证据；等待单独合并授权。

## 自检与后续边界

缓存/空间/长度/哈希对应任务 1；HTTPS/超时/有界内存/取消对应任务 2；可信身份/有效期/渠道对应任务 3；进度/语言/主题/退出对应任务 4；运行目录及整体验收对应任务 5。类型与测试目标由各自首次出现的任务定义，不引入动态任意 URL 接口或断点续传。

下载后的下一阶段是便携更新助手、解压边界与可恢复文件事务；安装版仍需独立 NSIS/UAC/隔离虚拟机验收。本计划不把包下载完成等同于自动升级完成。

## 实现参考

- [QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)：提交前临时写入、失败丢弃；禁用直接写回退。
- [QCryptographicHash](https://doc.qt.io/qt-6/qcryptographichash.html)：分块 SHA-256，不整包加载。
- [QNetworkReply](https://doc.qt.io/qt-6/qnetworkreply.html)：有界读取、abort 和异步完成事件。

## 本阶段启动记录

启动时 master 已快进到 beada989，无 merge 提交。下载阶段从该提交建立 codex/update-download，复用现有隔离工作树及构建缓存；初始计划提交本身不代表功能交付。

## 执行与验收记录

- 任务 1：f3eea8bd、c46922f4。流式缓存与哈希完成，32 项 QtTest 通过；审查后补齐正数短写变异验证及缓存根本身重解析点回归。
- 任务 2：f553a476。下载器 48 项、原清单获取器 36 项、缓存 32 项通过；共享 URL 检查，覆盖读取预算、重定向、超时与信号重入。
- 任务 3：84452777、90d98b9a。协调服务 43 项通过；修正 packages-v1 路径并用变异测试证明生产/测试验签来源隔离。
- 任务 4：31c39ecd。下载/取消/重试与进度接线完成，聚焦回归 22/22，原生 Windows 150% 界面测试 18/18。长说明曾使进度条滚出视口，补失败测试后固定进度区；三语言、双主题、四状态 24 张截图布局通过。
- 任务 5：主控完整 Release 构建通过，最终 CTest 79/79（53.08 秒）。runtime 重建为 41 个文件，无测试工具或新增框架 DLL；另将最终 runtime 复制到隔离测试目录，以仅含运行目录的 PATH 启动，打开两份测试日志并通过存储隔离断言。
- 四项任务均经过独立只读审查，缓存及协调层各完成一轮定向修复复审。整分支审查无 Critical/Important；公共存储工厂的内部化为非阻断维护建议，当前无用户注入入口。

实现中的必要补强：VerifiedManifest 增加不可变的实际验签环境与密钥来源，下载服务核对其与当前配置一致；原类型丢失这些信息，不能充分表达测试/生产信任隔离。此选择的代价是验证核心接口变更与依赖目标重编译，没有增加生产信任根。

真实生产 HTTPS、正式发布身份、便携事务、NSIS/UAC 和跨平台实际运行未验收。默认生产服务仍未配置，用户数据、主工作区文件和 master 未修改；分支保留，合并与推送须单独授权。
