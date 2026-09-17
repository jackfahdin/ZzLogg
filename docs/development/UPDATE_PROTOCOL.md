# 更新协议、检查与下载

## 当前完成范围

静态 C++ 验证核心提供版本解析、Ed25519 验签、严格清单解析、防回放和发布载荷选择。核心不依赖 Qt；独立 Qt 层已实现 HTTPS 检查、状态持久化、包下载与校验，主程序已有对应设置、检查和进度界面。更新代码不写注册表、不替换安装目录，也不修改用户日志数据。

**这不代表生产在线更新已上线。** 正式托管、公钥和发布流水线输入尚未配置；安装器执行和失败恢复尚未实现。已确认只给安装版提供在线更新，便携版手动更新，不开发便携自动替换。当前只支持全量包下载，不支持增量更新或自动安装。范围及实施顺序见 [仅安装版在线更新设计](../superpowers/specs/2026-09-15-installer-only-update-design.md)。

## 代码入口

| 位置 | 职责 |
| --- | --- |
| src/update/include/zzlogg/update/version.h | YY.MM.PP 严格解析与数值比较 |
| src/update/include/zzlogg/update/signature.h | Ed25519 原始消息验签 |
| src/update/include/zzlogg/update/manifest.h | 信任上下文、只读已验证清单、接受记录 |
| src/update/include/zzlogg/update/policy.h | 当前安装信息与发布选择结果 |
| src/update/include/zzlogg/update/releaseidentity.h | 编译生成的正式发布身份读取；开发构建返回空 |
| src/update/src/strictjson.cpp | JSON 重复键、深度、编码与规范 Base64 |
| src/update/src/manifest.cpp | 内部签名封装验证，不授予安装资格 |
| src/update/src/payload.cpp | 完整负载、期限、域名与防回放 |
| src/update/src/policy.cpp | 版本、系统、架构、便携/安装版及数据格式选择 |
| tests/update | 标准向量、真实签名和错误边界测试 |
| tools/update | 默认关闭的离线测试清单工具 |
| src/updateqt | Qt 检查服务、状态存储、流式包缓存与下载协调 |
| src/updateqt/include/zzlogg/updateqt/installedrelease.h | 纯发布身份与只读安装身份组合，不执行系统探测 |
| src/ui/src/updatecheckdialog.cpp | 检查、下载进度与用户操作窗口 |
| tests/updateqt | 脚本网络、真实验签/缓存及生命周期回归 |

Monocypher 4.0.3 和 nlohmann/json 3.12.0 均为固定版本离线依赖。原始文件摘要、许可证与来源记录位于各自 vendor 目录；不增加运行时 DLL。

## 字节级签名格式

外层 JSON 只允许以下四个字段：

| 字段 | 规则 |
| --- | --- |
| schema | JSON 整数 1，不接受 1.0 |
| keyId | 1–64 个 ASCII 字母、数字、连字符或下划线 |
| payload | 标准 Base64，使用规范的 = 填充 |
| signature | 标准 Base64，解码为 64 字节 |

签名消息是下列字节拼接，换行均为单字节 LF：

```text
ASCII("ZzLogg update manifest v1\n") || ASCII(keyId) || ASCII("\n") || rawPayloadBytes
```

负载必须保持原始字节，不排序字段、不重新序列化；一个空格也会改变签名。公钥必须来自应用提供的可信表，不能由清单自带。验证顺序为：外层结构 → 可信键与用途 → 原始字节签名 → 负载结构与策略。

封装最多 256 KiB，解码负载最多 128 KiB，JSON 容器嵌套最多 16 层。重复键（包括转义后等价的键）、BOM、非法 UTF-8、原始 NUL、注释、尾随 JSON、非规范 Base64 一律拒绝。

## 负载字段

全部字段必需，未知字段拒绝；协议扩展必须升级 schema。

| 字段 | 类型与规则 |
| --- | --- |
| schema | JSON 整数 1 |
| product | com.gitcode.jackfahdinqt.zzlogg，协议产品标识，不是公司显示名 |
| metadataSequence | 正十进制字符串，uint64，无前导零、符号或小数 |
| issuedAt / expiresAt | 非负 JSON 整数，UTC Unix 秒，最大 2^53−1 |
| channel | stable 或 preview |
| releaseSequence | 与 metadataSequence 相同的数字格式 |
| version | YY.MM.PP，每段两位 ASCII 数字，月份 01–12 |
| minUpdaterProtocol | 正 uint32 整数；当前协议版本为 1 |
| minDataSchema / maxDataSchema | uint32 整数，前者不能大于后者 |
| notes | 仅 en、zh_CN、zh_TW，分别最多 16 KiB 的纯文本 |
| artifacts | 1–16 个载荷，os/arch/distribution 组合唯一 |

发布说明是文本数据，现有 UI 按纯文本显示，不交给 HTML 或富文本解释器。

### 发布载荷

| 字段 | 规则 |
| --- | --- |
| os / arch | 1–64 个小写 ASCII 字母、数字、连字符或下划线；首期选择 windows/x64 |
| distribution | portable 或 installer |
| format | portable 对应 zip；installer 对应 nsis-exe |
| minOsVersion | 三段非负 uint32 十进制数，如 10.0.19041，无前导零 |
| url | HTTPS、ASCII DNS 域名；只接受默认端口或显式 443 |
| size | 正十进制字符串，最多 512 MiB |
| sha256 | 64 个小写十六进制字符 |

URL 允许标准查询串，拒绝用户信息、片段、控制字符、反斜杠、非法百分号和域名后缀欺骗。主机转小写后与调用方提供的小写 DNS 允许列表精确比较；尾随点不接受。地址长度最多 8192 字节。

清单验证器校验签名清单内的 URL；下载器另对**每次重定向及最终地址**重新检查，并限制下载大小、校验完整包 SHA-256。清单通过不代表包已经下载完成，也不授予安装权限。

## 信任与状态边界

生产环境拒绝 Test 用途键、空键表、重复 keyId、非法长度以及本仓库公开 RFC fixture 公钥；即使把 fixture 公钥误标为 Production 也拒绝。

应用端必须从编译时确定的配置构造生产上下文，不得在用户设置、命令行或下载文件中提供切换到测试信任环境的开关。核心接口支持 Test 环境是为了独立自动化测试；它不是抵御调用者主动改写程序的安全沙箱。

生产密钥及其轮换、撤销机制尚未上线；仓库不包含生产私钥。不得把测试种子改名后用作发布密钥。

### 防回放

每个渠道分别保存 AcceptedMetadata：

- sequence：已接受的元数据序号。
- payloadDigest：原始负载字节的 SHA-512，不是签名字节的摘要。

低序号拒绝；同序号同摘要允许重复检查；同序号不同摘要拒绝。更换可信 keyId 并重新签署相同原始负载，不改变摘要。

调用方只能在 verifyManifest 返回 value 后持久化其中的接受记录。结构合法但没有适用载荷的清单仍需保存接受记录，不能因为 selectUpdate 未发现新版就丢弃。updateqt 适配层使用锁内重读和原子替换保存双渠道状态；成功落盘后检查服务才发布结果。

### 时间与发布选择

允许签发时间最多领先当前时间 300 秒。到期时间必须大于当前时间与签发时间，有效期最多 30 天。本地时间比构建时间早超过一天时拒绝。

发布选择检查渠道、正式/开发构建、更新协议、数据格式、发布序号和显示版本，再匹配系统、架构、分发类型和最低系统版本。升级要求发布序号与显示版本**同时增大**；同序号不同版本或更大序号却版本不增大视为冲突。

VerifiedManifest 只能由完整验证器构造。失败结果没有 value，因此不能被策略层拿来选择载荷，也不能取得接受记录推进状态。

## 离线测试工具

仅测试使用，默认关闭，不安装、不部署到 runtime，不联网。测试程序可依赖 Qt Core，核心静态库仍不依赖 Qt。

```powershell
cmake -S . -B out/ui-vs -DKLOGG_BUILD_UI_TESTS=ON -DZZLOGG_BUILD_UPDATE_TEST_TOOLS=ON
cmake --build out/ui-vs --config Release --target zzlogg_update_testfeed

# 将本机 Qt bin 加到当前终端 PATH；不修改系统环境变量。
$env:PATH = 'D:/SoftWare/Qt/6.11.0/msvc2022_64/bin;' + $env:PATH
& out/ui-vs/output/Release/zzlogg_update_testfeed.exe generate --output out/test-manifest.json
& out/ui-vs/output/Release/zzlogg_update_testfeed.exe verify --input out/test-manifest.json --now 1800000000

ctest --test-dir out/ui-vs -C Release -R '^zzlogg_update\.' --output-on-failure
```

示例假设已有 Windows VS 构建目录；首次配置参照 [编译文档](../BUILD.md)。generate 使用独占创建，拒绝覆盖已有文件。verify 同时确认同一测试文件在生产信任环境下被拒绝。

固定 fixture：版本 26.10.00、元数据/发布序号 2、签发时间 1799999900、到期时间 1800003600、测试主机 updates.example.invalid。包大小和哈希仅用于结构测试，不对应真实发布包。

退出码：0 成功，2 参数错误，3 文件错误或超限，4 验证拒绝，5 生产信任隔离检查失败。测试输出只写入构建树，不接触用户配置。

## 验收与下一阶段

下载阶段 Windows Release 完整构建通过，CTest 79/79 通过，Windows 原生 150% 更新界面测试 18 项通过。三语言、双主题、四种下载状态及超长说明共保存 24 张原生截图，进度区和底部按钮保持可见。跨平台实际运行及生产 HTTPS 互通未验证，不宣称已通过。

Qt 网络获取、渠道状态存储、统一检查服务、下载服务、进度界面和应用接线已完成。帮助菜单与设置页共用应用级服务和检查窗口；设置中的渠道及自动检查选项在应用后生效。下载只由明确点击启动，后台检查不会自动下载；重新检查或换渠道使旧下载失效。关闭窗口或销毁其父窗口会取消活动检查和下载，迟到响应不会重新弹窗。界面区分网络、磁盘、长度及哈希失败，允许取消后重试；完成仅提示已验证，仍不提供安装操作。

生产地址与公钥尚未提供，当前程序显示“更新服务尚未配置”，不发网络请求；真实生产 HTTPS 互通尚未验证。测试信任仅存在独立测试程序，不部署到运行目录。

下载、缓存和校验后端已实现；安装版受保护暂存、文件替换事务、安装器提权、恢复及正式发布签名工具仍未实现。按安装身份基础、安全交接、安装事务及真实发布验收推进，不实现便携执行器。第一段任务见 [安装身份基础计划](../superpowers/plans/2026-09-15-installer-identity.md)。包已下载不代表可以执行或安装。

## 更新包下载与缓存

下载服务只接受仍有效的已验签检查结果，重新按明确的发布身份选择载荷，不信任界面传入的公开 decision。已验签结果保留实际验签环境和密钥来源，必须与下载服务配置一致。当前开发构建没有正式发布身份，不能据此下载或推断便携/安装类型。

生产缓存来自系统 `QStandardPaths::CacheLocation`，固定追加 `updates/production/packages-v1`，不随日志路径或用户数据目录变化。测试缓存使用 `updates/test/packages-v1`，且要求 Qt test mode。路径工厂不创建目录；只有明确下载操作通过信任检查后才开始写入。

- 文件名由 SHA-256 生成，忽略 HTTP 文件名；根、祖先、目标及锁路径拒绝符号链接和 Windows 重解析点。
- 包上限为 512 MiB，预检缓存卷可用空间至少为包大小加 16 MiB；实际写入仍处理短写、磁盘满和提交失败。
- 使用同名锁和 `QSaveFile` 临时写入，禁止直接写回退。长度与 SHA-256 同时匹配后才提交并提供可用路径。
- 失败、取消、未完成析构只清理本次未提交写入，不递归删除缓存根，不清理未知文件，不损坏原有同名包。
- HTTPS 最多 3 次允许主机内的重定向，最终响应必须为 HTTP 200；拒绝压缩编码、分段响应、长度不符及 TLS 错误。无传输超时 30 秒，总时限 30 分钟。
- 单次读取最多 64 KiB，reply 缓冲配置 128 KiB，每轮消费预算 1 MiB 后让出事件循环；进度以实际写入字节计算，不整包加载内存。

本期不支持断点续传。取消或失败后重新下载；缓存存在本身不是信任依据。安装阶段必须重新验签、验包，不能直接执行缓存路径。并发、换渠道、过期及迟到响应不会授予新的可用包状态。隔离脚本网络测试不等同于生产 HTTPS 互通验收。

## 检查服务的编译配置

以下 CMake 缓存项默认均为空。没有可用地址、允许主机和可信公钥时，检查服务返回“尚未配置”，不发送请求，也不报告“已经是最新版本”。

| 配置项 | 内容 |
| --- | --- |
| ZZLOGG_UPDATE_STABLE_URL | 稳定渠道签名清单的 HTTPS 地址 |
| ZZLOGG_UPDATE_PREVIEW_URL | 预览渠道签名清单的 HTTPS 地址 |
| ZZLOGG_UPDATE_PUBLIC_KEYS | 分号分隔的 keyId:公钥；公钥为 64 个小写十六进制字符 |
| ZZLOGG_UPDATE_ALLOWED_HOSTS | 分号分隔的小写 DNS 主机名，精确匹配，不使用通配符 |

配置只进入编译生成的内部头文件，不从用户设置读取。字符串先转换为十六进制，避免直接拼入 C++ 字面量；密钥 ID、长度、重复 ID 和 DNS 标签在配置时校验。私钥不属于应用配置，也不能放入仓库。

没有正式发布序号及可信部署身份的构建，只能展示通过验签的发布信息，不能推断可以安装，也不会根据用户的数据保存目录猜测分发类型。网络层最多跟随 3 次允许的 HTTPS 重定向、接收 256 KiB 清单，整次请求限时 30 秒；无传输超时为 10 秒。

## Windows 安装身份只读探测

安装身份基础提供纯证据判定和 Windows x64 只读探测。生产入口使用当前进程的
`QCoreApplication::applicationFilePath()`，只读取 HKLM 64 位视图的
`Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg`；不读取 HKCU 或 32 位视图，
不接受命令行、用户设置或下载文件覆盖。登记键不存在与读取失败分别处理。

`InstallLocation` 只接受有界 UTF-16 的 `REG_SZ` 或系统展开后的 `REG_EXPAND_SZ`，
兼容没有终止 NUL 的合法字符串，但拒绝内嵌 NUL、非法 UTF-16、奇数字节长度、
空/相对路径、超过 64 KiB 的值以及两次查询间的类型/长度变化或读取失败。
`UpdateIdentitySchema` 必须是精确 4 字节的 `REG_DWORD`；缺失时只可能判为 Legacy，
只有值 1 且所有路径与标记证据通过时才判为 Registered。

路径检查先检查原始拼写和各层目录、程序、标记的 Windows 属性，再规范化比较。
祖先、安装根、程序和标记的重解析点均拒绝；程序须为普通 `ZzLogg.exe` 文件。
目录匹配同时要求规范路径忽略大小写相等，以及目录句柄的卷号、文件编号相等，
因此普通目录的大小写变化可匹配，NTFS 大小写敏感目录中的两个不同目录不会混同。
`.zzlogg-install-root` 必须是普通文件，最多 256 字节，内容是 UTF-8 的
`ZzLogg <非空可打印构建文本>` 加 CRLF 或 LF；拒绝 NUL、控制字符、多行、非法编码与读取错误。
版本文本按完整输入一次性解码，拒绝尾部截断 UTF-8，并保留 BOM 交给可打印性检查拒绝。
标记中的文本不用于构造 `InstalledRelease` 或正式发布身份。

当前仅支持普通盘符绝对路径。UNC、设备/扩展路径（如 `\\?\`）、盘符相对路径、
根相对路径、`.`/`..` 分量、重复分隔符、尾随点/空格、额外冒号（含备用数据流）与
通配符等歧义拼写会返回 Invalid；单个尾随分隔符可以接受。其他 Windows 架构及
非 Windows 的生产入口返回 Unsupported。这些情况和 Legacy 安装暂时仍需手动更新，
不能将失败关闭解释为安装已损坏。长路径和其他文件系统未做实际兼容性验收。

探测结果只是读取时的身份一致性观察，不是后续操作的授权，也不保证检查到使用之间
路径、文件或登记不会变化。安全交接阶段仍须对签名、文件稳定性、路径和安装身份重新验证。
当前应用未接入此探测来生成发布身份，生产安装动作仍关闭；没有新增安装器执行、
UAC、文件替换或注册表写入。真实 NSIS/UAC 安装验收尚未执行。

隔离测试使用 QTemporaryDir 和外部注册表替身，实际运行生产原始数据解析器与真实文件系统，
覆盖登记/复制目录、标记格式和大小、读取拒绝、畸形注册表数据、查询失败竞态、
根与祖先 junction 以及 NTFS 大小写敏感目录。文件符号链接测试在当前环境无法创建时
明确跳过；清理通过原生非递归 API 只删除测试自身链接，不遍历链接目标。

本期 Windows Release 完整构建通过，CTest 81/81 通过；两个安装身份目标通过，
其中判定矩阵 QtTest 为 14 项通过，探测 QtTest 为 76 项通过、2 项文件符号链接创建受限而跳过
（这些 QtTest 数量均含初始化和清理）。根/祖先 junction 和大小写敏感目录测试实际执行通过；
未将跳过项计为功能验收通过。

## 正式构建发布身份与组合

3B.1 提供默认关闭的编译期发布身份。`ZZLOGG_OFFICIAL_RELEASE` 默认为 `OFF`；只有正式发布流水线显式开启并提供全部字段时才生成身份，残留字段不会让开发构建获得正式身份。

| 配置项 / 字段 | 规则 |
| --- | --- |
| `ZZLOGG_DISPLAY_VERSION` / `version` | `YY.MM.PP`，每段两位 ASCII 数字，月份 01–12；非零第四版本分量拒绝 |
| `ZZLOGG_RELEASE_SEQUENCE` / `releaseSequence` | 1–18446744073709551615 的规范十进制，不接受前导零、符号、空白、小数或指数 |
| `ZZLOGG_RELEASE_CHANNEL` / `channel` | `stable` 或 `preview` |
| 编译目标 / `os`、`arch` | 仅 Windows x64，生成值固定为 `windows`、`x64` |
| `ZZLOGG_RELEASE_DATA_SCHEMA` / `dataSchema` | 0–4294967295 的规范十进制；0 只能由流水线显式声明，且不表示现有配置文件已有统一 schema |
| `updaterProtocol` | 当前固定为 1（清单协商能力号，与线协议号独立演进；裁决见 3C 节） |

无 Qt 核心读取上述编译常量；非 Windows x64 的编译结果仍返回空。Qt 适配层的纯函数只在发布身份再次通过字段校验、安装身份是 `Registered` 且根目录非空、调用方传入的操作系统主版本非零时，组合出 `Installer` 类型的 `InstalledRelease`。它不读取注册表、文件标记、用户配置或数据目录，也不自行探测操作系统版本。

正式发布身份只是用于比较的元数据，**不等于安装授权**。主程序目前仍向检查服务和下载服务传入 `std::nullopt`，没有把组合函数接入应用，也没有开放自动安装。3B.2 的基础与 3B.3 的受限进程基础见下节；生产签名验收、3B.4 的保存/退出/UI 接线以及 3C 的受保护安装事务和恢复仍未完成；在这些门禁和生产配置完成前，不得启动安装器或写入 HKLM。

## 执行前包验证基础（3B.2）

`zzlogg_update_execution` 是独立的 Windows 静态目标，只依赖无 Qt 核心和 Windows 系统库；
其他核心消费者不因此链接执行层。非 Windows 不构建此后端，Windows 非 x64（含 ARM64EC）
的组合入口返回 Unsupported。ARM64/ARM64EC 回归只在主机编译器上使用受控目标宏，
不代表在那些架构上实际执行过测试。

`verifyPackageForExecution` 使用调用方提供的当前可信状态，顺序执行：原始签名信封重验并
重新选择同一安装包、只允许 Production trust、检查内置发布者策略、稳定打开、同句柄大小
和流式 SHA-256、Authenticode 信任及发布者匹配、再次核对路径和句柄身份。它不读取用户
注册表，不信任以前的 `verifiedPath` 或缓存成功状态。生产发布者证书 DER SHA-256 允许列表
目前为空，任何通过选择重验的生产输入仍返回 PublisherPolicyMissing；清单、命令行、
环境变量和用户配置都不能提供替代策略。

稳定文件层仅接受少于 MAX_PATH 的普通绝对盘符路径及固定本地盘；拒绝 UNC、设备路径、
相对路径、ADS、NUL、点段、空分量、尾随点或空格、通配符和 DOS 设备名。从盘根到父目录
逐级保持不共享删除的目录句柄，检查每级不是 reparse；叶文件只用 GENERIC_READ 和
FILE_SHARE_READ 打开，要求 DISK、普通文件且只有一个硬链接。最终规范路径、卷号和文件编号
必须与稳定句柄一致。租约持续持有文件和所有目录句柄，移动后源租约失效，失败和析构释放资源；
公开的 `VerifiedPackage` 不能被调用方构造，也不暴露可关闭的原始句柄。

目录的元数据写句柄不等于普通文件写入，不能只用“打不开写句柄”证明目录安全。真实回归保留
每级子对象，阻止文件及父/祖先目录移走；在父目录和祖先目录分别用 GENERIC_WRITE 与
FILE_WRITE_ATTRIBUTES 取得句柄，实际设置 junction 均得到 ERROR_DIR_NOT_EMPTY，
之后路径和文件身份仍一致。已有写/删文件句柄，以及关闭原文件句柄后仍保留的可写映射，
均使稳定打开失败。租约释放后写入、重命名和删除恢复可用。其他文件系统和长路径兼容性
仍需另行验收。

大小要求为 1..512 MiB，哈希为规范小写 64 hex；每次读取最多 64 KiB，检查读取错误、总字节数
与 SHA-256。大小和哈希一致不构成签名信任。WinVerifyTrust 使用
WINTRUST_ACTION_GENERIC_VERIFY_V2、WTD_UI_NONE、完整路径和同一个读句柄；只接受
LONG 精确等于 0，要求撤销检查（链排除根），离线或未知撤销结果不降级。主签名者叶证书
来自此次已验证 provider state，计算 DER SHA-256 与允许列表逐字节匹配，不比较显示名称，
不使用时间戳 countersigner 代替。WTHelper 等入口仅从 System32 的 wintrust.dll 动态解析，
缺失即拒绝，每次尝试均关闭 WinTrust state。

测试将 API 替身限制在测试目标，只验证真实 Authenticode 算法的状态码、参数、主签名者
指纹与关闭行为；替身不能生成生产 VerifiedPackage。真实无签名文件拒绝、junction、硬链接、
共享冲突和可写映射回归均实际执行；文件符号链接在本机因权限不足明确跳过。
移除叶文件共享保护的变异触发四项回归失败，恢复后通过。

此阶段没有安装能力，也没有启动安装器、UAC、进程交接或修改信任库/HKLM。
真实签名链成功、正式证书指纹配置、证书轮换与撤销/离线验收、管理员阶段独立重验、
实际启动到安装器读取全过程的稳定性仍是上线门槛。普通权限成功租约不是管理员授权；
用户可写缓存与交接消息都不能成为管理员信任根。VerifiedPackage 也不是可永久复用的授权：
调用方必须在每次尝试时重新取得 freshContext；3B.3 的实际 launch 仍需完成时效、渠道和
可信状态变更的协调，不能把此阶段某次验证成功无限期保留后再启动。

## 独立交接进程基础（3B.3）

仅 Windows x64 构建 `ZzLoggUpdate.exe`。其执行核心、加密库和交接库整个依赖闭包使用
静态 CRT，应用原有动态 CRT 配置不变。实际 PE 的普通及延迟导入表、静态库链接指令均进入
自动验收；生产程序复制到隔离目录、PATH 仅保留 System32 后也能完成关闭门禁检查。
不部署测试 fixture，不增加 ARM64/ARM64EC 支持，不修改应用 UI 或当前部署目录。

正式入口仅接受一个十进制继承 bootstrap 映射句柄。固定有界、只读的匿名映射携带协议版本、
随机事务 ID、32 字节随机令牌、父进程句柄/PID/创建时间；管道名由事务 ID 唯一推导。
映射与父进程查询/等待句柄通过 STARTUPINFOEX 显式允许列表继承，令牌不进入命令行或日志。
bootstrap 身份通过后，生产程序发送 Hello、验证 Ready，然后发送 Failed 并返回
`ExecutionDisabled`（退出码 40）；缺失/非法 bootstrap 返回 `BootstrapRejected`（41）。
生产程序没有安装器路径参数、测试开关、ShellExecute/runas 或启动新版 GUI 的入口。
3B.2 发布者策略仍为空，既有 VerifiedPackage 私有构造及权限边界不变。

事务运行副本从稳定源句柄复制，限制为 1..512 MiB；目标使用 CREATE_NEW，目录以密码学随机名
独占创建，文件/目录 DACL 只授权当前登录 SID。逐级拒绝重解析点，保持祖先目录稳定租约，
刷新并验证目标字节数与 SHA-256，在 CreateProcess 前再次核验身份。源租约在启动前释放，
原文件在子进程运行时可重命名/替换；运行目录必须独立于待替换的安装目录。
源与副本一致只证明同源，不提供发布者或管理员信任。CreateProcess 使用显式绝对路径、
已知工作目录、CREATE_NO_WINDOW；线程级 error-mode 确保损坏 EXE 返回失败而不等待系统对话框。

目录使用系统 ntdll 中的 [NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile)
以已固定父目录为 RootDirectory、单个相对名字、FILE_CREATE|FILE_DIRECTORY_FILE 原子取得句柄，
只接受 FILE_CREATED。已存在名字及重解析条目均拒绝，避免 CreateDirectoryW 后另行打开之间的
替换窗口。系统缺失该 API 时失败关闭，不退回有窗口的创建路径；这一兼容性约束需在目标 Windows
环境验收。后续仍核验最终路径及身份，不以原子创建代替内容或发布者验证。

本地消息管道启用 FIRST_PIPE_INSTANCE、REJECT_REMOTE_CLIENTS、消息模式和 overlapped I/O，
DACL 仅授权精确登录 SID；客户端使用 SECURITY_IDENTIFICATION。双方核验实际端点 PID、
持续持有的真实进程句柄、创建时间、用户 SID、登录 SID 和会话。每次连接/读/写接收一个绝对
截止时间；超时 CancelIoEx 后等待该操作完成，再销毁 OVERLAPPED、事件和缓冲区。
只接受协议的 60 字节固定消息，截断/超长、错凭据、重放、错序均关闭且不无限重试。

私有 `Coordinator` 组合运行副本、真实进程身份、管道及 HandoffSession，供后续适配和独立测试
消费；其中路径参数表示普通用户协调进程/测试替身，不授予安装能力。`start()` 或 Hello/Ready
成功均不能授权应用退出，只有活着且已认证的对端 AwaitingAppExit 才允许 `canCommitExit()`。
取消、握手失败/超时或提前退出均失败关闭。Complete 消息后还要等待真实进程退出，不能把窗口
关闭或一条退出消息当作进程结束；已提权宿主返回 `ManualRestartRequired`，本阶段不执行重启。

`finish(deadline)` 在对端仍存活时返回 `PeerRunning`。取消/超时后析构立即断连，并将该进程和
运行副本租约交给内部清理线程，避免调用线程无限等待；清理线程不携带线程归属的 InstallLock。
创建清理线程失败时保留句柄及可诊断残留到宿主退出，不提前清理活进程文件。
子进程在 bootstrap 中独立持有自身及祖先租约，父进程意外退出也不会使活着的副本失去保护。
生产 main 的握手使用单个 3 秒总截止时间；完整退出后只按持有过的文件/目录身份删除精确文件
及空目录，不递归清理用户路径。父进程崩溃残留的恢复与持久事务日志仍归 3C。

真实测试覆盖成功、取消、CreateProcess 失败、超时、双向错误进程、错误创建时间、消息攻击、
父进程/对端提前退出、退出消息后仍存活、原文件替换及孤儿自身租约。移除退出守卫或创建时间
比较的变异均触发失败，恢复后通过。这些替身成功不代表正式签名、真实 UAC、管理员安装事务
或跨账号安装器身份验收；3B.4 尚须接入保存/退出/所有启动入口，3C 才实现真实安装与恢复。

## 应用准备与交接接线（3B.4）

应用侧把可取消的退出准备、同安装目录活动租约和 3B.3 的受限原生协调串成异步交接，
仍不开放真实安装。

- `KloggApp` 提供 `prepareApplicationExit()` / `cancelApplicationExitPreparation()` /
  `commitApplicationExit()` / `isApplicationExitPrepared()`：保存全部窗口并同步会话
  成功后进入 Prepared，窗口保留但冻结快照变更（交互、新建窗口、载入文件、拖放、IPC、
  托盘、关闭与普通退出/重启均被拦截并恢复原 enabled 状态）；提交必须处于 Prepared，
  任何提交前失败撤销整个准备。
- `InstallationActivity` 以操作系统句柄生命周期实现只读活动租约（不写安装目录）；
  `reserveUpdate()` 在与 3B.3 InstallLock 相同的目录身份 mutex 下证明没有其他实例后
  完成预留，`cancelUpdate()` 先恢复本实例租约再释放 mutex。GUI 在单实例转发与
  `--multi` 之前、grep 在存储引导之前进入活动租约；观察到预留或身份异常时保守阻塞
  并提示手动关闭，不跨进程强关、不按进程名终止。KDSingleApplication 实例名按安装
  目录身份隔离，同目录（任意大小写）转发、不同目录独立。
- `ApplicationUpdateHandoff`（Qt 控制器）按 Idle→Preparing→Waiting→ExitCommitted
  及 Cancelled/Failed 管理生命周期，UI 只消费枚举快照。先准备/同步，再在同目录预留，
  然后在工作线程有界驱动原生 Coordinator；GUI 线程不阻塞在 connect/read/write/进程
  等待。代次与接收方生命期隔离保证取消后的迟到完成不能提交退出；取消先在 GUI 线程
  恢复窗口，待对端取消完成后再释放预留。**执行可用性由生产后端给出且本期恒关闭**，
  不由 Verified 下载状态推导；生产构造没有会话工厂，`begin()` 一律拒绝且不执行安装。
  只有活着且已认证的 AwaitingAppExit 对端才允许提交退出；提交后由宿主显式安排进程
  退出（退出码 0，绝不用重启码 773）。
- 预留跨越主程序真实退出：bootstrap（版本 2）可选携带预留目录身份，子协调者在
  Hello 之前以 SYNCHRONIZE 打开并持有该身份对应的既有 mutex（观察句柄，不等待、
  不释放、不移动所有权）。父进程仍持有原句柄时完成绑定；父退出后对象经观察句柄
  存续，任何新 InstallLock/进入均失败关闭，直到子协调者结束。句柄存在不是安装授权。
- 更新对话框新增受能力约束的“退出并更新”：生产能力关闭时按钮隐藏且不能发可执行
  请求；检查/选择/下载变化都会清除已推入的能力。准备/等待期间检查、下载、跳过等
  改变选择的动作停用，关闭/ESC 等价取消。英/简/繁运行时切换与长文本布局有自动验收。
  便携副本仍仅提示手动更新。
- `ZzLoggUpdate.exe` 通过正式 CMake target 进入安装产物与 Windows runtime folder
  （按目标名显式复制，不扫描测试输出、不部署 fixtures）；运行目录契约验证其无
  Qt/MSVC 动态运行库依赖且入口仍拒绝任意参数。应用构建依赖该 helper。
  **这不是自动更新上线**：仍缺管理员安装事务与恢复日志（3C）、生产发布配置与真实
  UAC、真实升级验收；生产服务仍无正式发布身份，安装按钮默认隐藏。同目录其他实例
  采用保守阻塞策略，等待用户手动关闭，不做跨进程强制退出。

3B.4 基线曾出现协调者测试 0xc0000409：根因是测试复用进程临时目录名（PID 复用时
撞目录），修复为密码学随机目录名加原子创建（任务 0，仅修测试）。

3C 任务六对上述接线做了收口：参与者销毁导致的准备丢失经
`KloggApp::applicationExitPreparationLost` 外发，控制器收到后立即失效在飞完成
（代次失效）并以 PreparationFailed 失败关闭，消除"复检与在飞 CommitExit 之间"的
残余窗口；析构改为 15 秒有界等待，超时诊断日志后释放 GUI 线程预留并故意泄漏
工作线程（不阻塞应用拆卸、不与仍在运行的协调者竞争）；取消尚未释放预留期间的
重复 begin 返回可诊断快照（状态保持 Cancelled，失败枚举 CancellationInProgress）
而非静默 false；Abandoned 预留与 Blocked 在守卫诊断与三语文案上明确区分。

## 安装事务与恢复（3C）

3C 交付协议 v2、bootstrap v3、持久事务日志、登记事务引擎、NSIS 受限入口与协调者
生产链路。**生产门禁不变**：应用内组合真实协调者的生产会话工厂仍缺席，"退出并
更新"不开放生产可见性；本节全部机制只在专用测试目标中真实执行。

### 线协议 v2 与 Proceed

协调者与其对端（3B 的运行副本、3C 的事务引擎）之间的本地通道消息集显式补齐
Proceed（=8），版本字节升为 2。说同一协议的两端在真实部署中跨构建版本：引擎
编译进新安装包载荷，协调者是已安装旧构建的子进程，因此协议演进必须保持严格
失败关闭（见 updaterProtocol 裁决节）。消息
仍为 60 字节定长："ZZUP"、小端 uint32 版本、kind、16 事务字节、32 令牌字节；
截断/超长、错凭据、重放、错序一律关闭。状态机为 Connecting→Ready→
AwaitingAppExit→ExitCommitted→ExitConfirmed→Complete（或 Aborted）：ExitCommitted
只有经 Proceed 才升为 ExitConfirmed，Complete 只在 ExitConfirmed 合法。Proceed 由
协调者持有方在观察其**复制的应用进程句柄**真实退出后发送（不相信对端自报；
句柄以 adopt 防 PID 复用）；应用仍存活时查询返回 PeerRunning，已发送后幂等返回
ProceedSent。**引擎在收到 Proceed 之前不得修改安装目录，也不得报告 Complete**；
违约由 fixture 违约模式（engine-violation、early-complete）真实证明协调者拒绝。

### bootstrap v3

bootstrap 映射版本升为 3，新增 240 wchar 容量的 `dataDirectory` 可选字段：非空时
必须是绝对路径（本地盘或 UNC）、容量内 NUL 终止、不含控制字符；旧版本、畸形、
相对路径、未终止一律拒绝。数据目录只是事务后重启上下文，不是安装授权。v2 引入
的 DirectoryReserved 观察句柄语义不变。

### "准备→预留→握手→提交/取消"时序

1. 准备：`prepareApplicationExit()` 保存全部窗口并同步会话后进入 Prepared，
   窗口保留但冻结一切快照变更。
2. 预留：`reserveUpdate()` 在目录身份 mutex 下证明无其他实例后持有预留；
   Abandoned（前持有线程未释放即终止）与 Blocked（活实例）分别诊断。
3. 握手：工作线程有界启动运行副本，Hello/Ready 认证后等待 AwaitingAppExit；
   GUI 线程从不阻塞在通道 IO 或进程等待上。
4. 提交：仅 Waiting 且本地仍 Prepared、对端活着且已认证 AwaitingAppExit 才可
   提交；CommitExit 成功后才 `commitApplicationExit()`，随后宿主显式 `exit(0)`
   （绝不用重启码 773）。预留刻意保持：子协调者在 Hello 前以 SYNCHRONIZE 打开并
   持有预留 mutex 的观察句柄（不等待、不释放、不移动所有权），父进程退出后对象
   经观察句柄存续，任何新进入/加锁失败关闭，直到子协调者结束。
5. 取消：GUI 线程立即恢复窗口，对端取消完成后才释放预留；迟到完成被代次
   隔离丢弃，永远不能提交退出。
6. 准备丢失：参与窗口销毁使 KloggApp 取消准备并发出
   `applicationExitPreparationLost`，控制器失效在飞完成、取消会话、释放预留，
   以 PreparationFailed 失败关闭。
7. 应用真实退出后协调者发 Proceed，引擎才开始事务；Complete 后协调者复核
   登记/标记/清单，以原用户身份重启应用（仅 `--data-dir` 参数，目录作用域
   单实例端点有界确认）；已提权宿主返回 ManualRestartRequired，不自动重启。

### 事务日志与受保护事务目录

追加式持久事务日志（txjournal_win）：32 字节头 + 68 字节定长记录头 + 长度前缀
UTF-16 体，显式小端编码。打开时以原子 NtCreateFile 独占创建 `<根>\<txid 十六进制>`
事务目录（祖先钉住防改名/重解析，ACL 主体可注入；生产为 Administrators/SYSTEM
写 + 已验证用户读）。每条记录先写后刷（FlushFileBuffers，可注入观察）再执行其
描述的操作；Complete 后拒绝追加；任何写/刷失败锁死日志。重放严格 fail-closed：
撕裂尾、未知操作、未知 flags、序号缺口、事务 ID 不符均判 Corrupt 且重放为零
操作；重放只读幂等。卷空间预检采用饱和加法、1 MiB 日志预留和最近现存非重解析
祖先查询，暂存卷与备份卷分别预检。

### 登记事务引擎与恢复边界

引擎（txengine_win）先写日志后操作：清单（ZZTXMAN1：16 字节头
magic+version(1)+count，随后逐条目）有界敌意解析，与应用侧 NSIS 清单生成器
逐字节同源（共享枚举生成器）。普通失败逆序回滚；中断（杀进程/断电点）后经
授权路径幂等恢复；登记白名单精确到卸载项键，要求 `UpdateIdentitySchema==2`；
Corrupt 或 0 字节日志呈现为 NeedsAuthorizedRecovery（保留现场，交授权恢复，
绝不自动继续）。退出码：Applied/Recovered/NothingToRecover=0、Rejected=42、
Conflict=43、RolledBack=44、NeedsAuthorizedRecovery=45、RecoveryFailed=46。

### 落地清单 schema 2 与 NSIS 受限入口

新安装写入 `UpdateIdentitySchema=2`（HKLM 卸载项）；schema 1 或缺失按 Legacy
处理，引擎对非 2 一律 Rejected。NSIS 提供两个受限入口：
`/ZzLoggUpgrade=<16 位小写 hex 定位名>` 与 `/ZzLoggRecover=<定位名>`。两种模式
都禁止 `/D=`（受限运行绝不改变登记目标）、在任何页面显示前 Quit、VerifyTarget
逐级拒绝 reparse、使用受保护事务目录并传播引擎退出码。升级模式按定稿 argv
契约以五组 flag/value（--install/--staging/--txroot/--txid/--version）启动引擎；
定位名格式在协调者、NSIS、引擎、凭据文件四方逐字节一致。

目录布局（3C 最终审查修正）：journal 事务目录 `<根>\<定位名>` 只归引擎所有，
由 TxJournal::open 独占创建——安装器只做存在性 busy 探测，绝不预建（引擎对
已存在目录一律 Exists→Rejected，不存在"采纳预建目录"）。安装器创建并加固的
是受保护根 `%ProgramData%\ZzLogg\UpdateTransactions`（仅在首次创建时执行
`/inheritance:r`，Administrators/SYSTEM 完全 + Authenticated Users 只读）与
同级兄弟暂存目录 `<根>\staging-<定位名>`（Administrators/SYSTEM 完全 +
Users 只读），载荷引擎、新版清单与 nsis-entry.log 都落在暂存目录。引擎
受保护镜像断言对根只接受这一精确 ACL 形状（属主 Administrators/SYSTEM，
除二者外无任何写位 ACE）；不放宽扫描规则，根形状不符即 Rejected，形状
建立的正确性由本清单的阶段 4 真实环境用例验收。

### 协调者生产链路

协调者→安装器/引擎的受限 bootstrap 使用凭据文件：当前用户私有临时目录下随机
定位名命名、CREATE_NEW、当前用户 DACL；引擎做属主、形状、定位名一致性、活
协调者身份四层校验，读后即删，失败路径同样清理。安装器经可注入 seam 以
ShellExecuteEx runas 启动，命令行只带受限开关（令牌不进命令行或普通日志）；
ERROR_CANCELLED 映射为 Cancelled（UAC 拒绝）。Complete 后协调者复核登记、标记、
清单一致，再以原用户身份重启应用；确认失败保留事务备份供授权恢复。

### 发布身份 updaterProtocol 与线协议号的关系（裁决）

**裁决：发布身份 `updaterProtocol` 保持 1，与线协议号独立演进。** 理由与边界：

- 发布身份 `updaterProtocol` 是清单协商能力号：发布清单的 `minUpdaterProtocol`
  与已安装发布身份比较（selectUpdate → ProtocolUnsupported），表达"安装此版本
  所需的最低更新执行能力"。该语义自 3B.1 起未变，并由
  installedrelease/releaseidentity 断言矩阵锁定为 1；3C 交付的是事务与恢复
  机制，没有新增需要清单侧协商的执行能力，升为 2 会虚假声明一个清单可协商的
  新能力。
- 线协议号（bootstrap v3、通道消息版本 2）是 ZzLogg.exe、协调者与事务引擎
  之间的内部契约，不经清单协商，其演进不需要发布身份号同步。但这条契约只有
  ZzLogg.exe↔ZzLoggUpdate.exe 一段两端同构建部署；引擎段在真实部署中必然跨
  构建版本——事务引擎编译进**新安装包**载荷，协调者是**已安装旧构建**的子
  进程，bootstrap 映射由旧协调者写入、新引擎解析（`ChildBootstrap` 严格校验
  version==3）。因此线协议或 bootstrap 升级必须保持新引擎对旧协调者的失败
  关闭：版本字节或 bootstrap 版本不匹配一律拒绝（decodeMessage/ChildBootstrap
  均严格失败关闭），任何跨版本兼容都必须显式设计并测试，不能默认假设同构建。
- 边界：仅当发布执行语义出现需要清单侧协商的变化（例如新的执行或恢复能力
  要求旧版本应用拒绝安装）时才升 `updaterProtocol`，并同步更新
  installedrelease/releaseidentity 测试矩阵；线协议（bootstrap/通道消息）变更
  本身不构成升级理由，但其跨构建失败关闭语义必须保持。

### Prepared 期 WM_QUERYENDSESSION 评估

Prepared 期间窗口 closeEvent 无条件 ignore。Windows 会话结束（注销/关机）时
系统向顶层窗口发 WM_QUERYENDSESSION，Qt 将其转换为关闭事件；被忽略即对本次
关机投否决票，系统呈现"应用阻止关机"界面并允许用户强制继续。评估结论：**有界
且可接受，不引入代码改动**。Prepared 窗口期被设计为秒级——交接到达 Waiting 后
KloggApp 立即提交退出；取消路径同步恢复；准备丢失通知保证参与者销毁时窗口期
立即结束。否决只发生在该窗口内，随后要么提交退出（进程真实退出，关机继续），
要么取消恢复（正常响应后续关机请求）。在 Prepared 期特化处理会话结束（例如
自动取消交接）会破坏"取消必须先恢复窗口、对端取消完成后才释放预留"的顺序
保证，并把会话结束竞争引入不可逆提交流程。真实关机/注销交互抽查列入阶段 4
验收。

### 3C 未交付项与阶段 4 真实环境验收清单

仍未交付（不宣称自动更新上线）：应用内组合真实协调者的生产会话工厂（"退出并
更新"生产可见性不开放）；生产发布者证书指纹允许列表（仍为空，任何生产输入
PublisherPolicyMissing）；生产签名工具与正式发布流水线；生产 HTTPS 地址与公钥
配置。

阶段 4 隔离环境验收清单（本机验收边界：不弹真实 UAC、不写真实 HKLM、不改真实
安装目录、不运行真实 NSIS 安装包）：

- 真实 UAC 提权链路（含用户拒绝路径）与跨完整性级别令牌/身份验证可行性；
- 真实 ProgramData ACL 树下引擎受保护根 accept 路径（upgrade 的 prepare
  与 recover 双模式，含根已存在/已加固的幂等进入）；
- 真实 HKLM 写入（卸载项、UpdateIdentitySchema=2）与登记回滚恢复；
- 真实 NSIS 安装/升级/恢复包端到端（VerifyTarget、禁 /D=、退出码传播、
  静默页面行为）；
- 真实安装目录文件替换事务与中断恢复（杀进程/断电注入点）；
- 分卷（暂存与备份不同卷）空间预检端到端（本机单固定盘环境受限）；
- NtCreateFile 原子目录创建与目录租约在目标 Windows 版本上的兼容性；
- 生产签名链成功、证书轮换与撤销/离线验收；
- 关机/注销（WM_QUERYENDSESSION）与 Prepared 窗口的真实交互抽查；
- 长路径与非 NTFS 文件系统兼容性（若声明支持）。

### 测试签名（阶段 4 工具链）

**红线：本节工具链仅测试用途，发布签名由生产流水线与真实证书承担。**
测试证书 Subject 含 `Test` 与 `NOT FOR PRODUCTION` 字样；任何 `.pfx`/私钥材料
绝不入库（仓库根 `.gitignore` 已覆盖 `*.pfx`）；证书与信任只写
`Cert:\CurrentUser\*`，绝不写 LocalMachine；验证结束后必须删除
`CurrentUser\Root` 中的测试根证书与临时 PFX，不长期信任无保护的测试根。

- `packaging/windows/New-ZzLoggTestCertificate.ps1`：生成自签名代码签名证书
  （`New-SelfSignedCertificate -Type CodeSigningCert`，EKU 1.3.6.1.5.5.7.3.3，
  有效期 2 年，私钥可导出）。`-TrustCurrentUser` 将证书导入
  `Cert:\CurrentUser\Root` 使本机 `signtool verify /pa` 通过；`-ExportPfx <path>
  -Password <SecureString>` 导出 PFX。重复运行按 Subject 复用已有证书（幂等），
  输出指纹与"仅测试用途"警告。
- `packaging/windows/Sign-ZzLoggArtifacts.ps1`：`signtool sign /fd SHA256`
  （`/f pfx /p pwd` 或 `/sha1 <指纹>` 二选一；仅指定 `-TimestampUrl` 时追加
  `/tr <url> /td SHA256`，默认不加时间戳以支持离线）。签名对象扩展名仅限
  `.exe/.dll/.msi`，其余一律拒绝。`-Verify` 只验签（`verify /pa`）。signtool
  定位顺序：PATH → vswhere → Windows Kits 10 bin 最新版，找不到以退出码 2
  报错并提示安装 Windows SDK；任何文件签名/验签失败即非零退出并列出失败文件。

典型流程：生成证书并导入 Root → 复制构建产物到临时目录（绝不签原始构建输出）→
签名 → `-Verify` 验签（退出 0）→ 篡改一字节后验签必须失败（非零）→ 清理测试根
证书与 PFX。生产签名（真实证书、时间戳服务、HSM/流水线凭据）不在本仓库与本
工具链范围内。
