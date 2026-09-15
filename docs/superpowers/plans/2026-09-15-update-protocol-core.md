# 更新系统第一阶段实现计划：协议与验证核心

> **面向 AI 代理的工作者：** 使用 executing-plans 在当前任务内逐项实现；每项遵循 TDD，完成验证后提交中文标题及详细说明。不自动创建新任务，不并行修改安全核心。

**目标：** 建立不依赖 Qt 的离线清单验证库，能够验证签名、严格解析协议、选择正确发布载荷，并提供隔离测试源生成工具。

**架构：** 主程序与未来的更新助手共用静态 C++ 验证核心。网络、持久化、解压、进程退出及文件替换不进入本阶段；所有环境信息通过参数输入，验证器不修改系统状态。

**技术栈：** C++17、C99、CMake、Monocypher 4.0.3、nlohmann/json 3.12.0、Qt Test（仅测试驱动）。

**规格：** [应用更新系统设计](../specs/2026-09-15-application-update-design.md)

## 全局约束

- 首期支持 Windows x64，保持现有 Qt 6 和 ZzPureTools 静态链接配置。
- 核心不得链接 Qt；测试程序可以链接 Qt6::Core 和 Qt6::Test，不使用 QApplication。
- 只支持全量载荷；本阶段不出现下载按钮、安装器或生产更新请求。
- 不修改用户数据、注册表、安装目录，不改变空的生产清单地址。
- 外部依赖固定版本、离线随仓库提供；不增加运行时 DLL。
- 测试密钥不得成为生产信任根，不生成或提交生产私钥。
- Windows 运行验证即可；不把未运行的其他平台测试标为通过。
- 当前提交为文档计划，执行前确认工作区状态；不占用已有其他任务的 worktree，不复制用户未跟踪图片。

## 协议 v1 的具体约定

### 封装

封装仅允许四个字段：`schema`（整数 1）、`keyId`（1–64 个 ASCII 字母、数字、连字符或下划线）、`payload`（标准带填充 Base64）、`signature`（标准 Base64，解码后 64 字节）。

完整封装最多 256 KiB，解码负载最多 128 KiB，JSON 深度最多 16。拒绝重复键、非法 UTF-8、尾随内容、BOM、注释及非标准 Base64；转义后的相同键也算重复键。

Ed25519 签名输入精确定义为以下字节拼接：

```text
ASCII("ZzLogg update manifest v1\n") || ASCII(keyId) || ASCII("\n") || rawPayloadBytes
```

换行是单个 LF 字节，不是反斜杠与 n；负载不重新序列化。公钥由调用方提供的受信任键表按 key ID 查找，封装内不能携带新公钥。通过签名之后才解析负载。相同 key ID 不得配置多个公钥。

### 负载

字段全部必需，v1 拒绝未声明的字段；协议扩展必须升级 schema：

| 字段 | 类型与规则 |
| --- | --- |
| schema | 整数 1 |
| product | 固定字符串 `com.gitcode.jackfahdinqt.zzlogg` |
| metadataSequence | 正十进制字符串，无符号/前导零，范围 uint64 |
| issuedAt / expiresAt | UTC Unix 秒，JSON 非负整数，最多 2^53−1 |
| channel | `stable` 或 `preview` |
| releaseSequence | 与 metadataSequence 相同的十进制格式 |
| version | `YY.MM.PP`，严格两位，月份 01–12，其他两段 00–99 |
| minUpdaterProtocol | 正整数，当前支持 1 |
| minDataSchema / maxDataSchema | uint32 整数，下限不能大于上限 |
| notes | 仅 en、zh_CN、zh_TW 三个字符串，每种最多 16 KiB；不渲染成 HTML |
| artifacts | 1–16 项载荷，每项字段见下表 |

| 载荷字段 | 规则 |
| --- | --- |
| os / arch | 首期匹配 `windows` / `x64` |
| distribution | `portable` 或 `installer` |
| format | portable 对应 `zip`，installer 对应 `nsis-exe` |
| minOsVersion | 三段非负十进制数，例如 `10.0.19041`；逐段比较 |
| url | HTTPS URL，ASCII 域名，允许标准查询串；拒绝用户信息、片段、控制字符、反斜杠、无效百分号及非 443 端口 |
| size | 正十进制字符串，首期最多 512 MiB |
| sha256 | 恰好 64 个小写十六进制字符 |

同一 os/arch/distribution 只能有一个载荷，不依赖数组顺序猜测。未知平台载荷可在严格结构校验后保留，但不能作为 Windows 候选。主机还必须精确匹配调用方的允许列表，不接受字符串后缀匹配；重定向的最终地址由下一阶段网络适配器再次执行同一规则。

每个渠道独立保存已见元数据序号和负载 SHA-512 指纹。低序号拒绝；相同序号、相同指纹允许重复检查；相同序号、不同指纹拒绝。已验证、结构合法但不适用当前平台的清单仍返回可持久化的元数据接受记录。无效签名、结构或时间不推进状态。

时间注入测试：允许签发时间最多领先本地时钟 300 秒；当前时间必须小于 expiresAt，且 expiresAt 必须大于 issuedAt，最长有效期 30 天。本地时钟显著早于构建时间（超过一天）返回 ClockInvalid；时间异常不能绕过验证。

升级必须同时满足发布序号增大和显示版本数值增大。相同发布序号而版本不同属于冲突；序号更大但版本不增大拒绝作为更新。开发构建及数据格式不兼容仅返回不允许安装的结果。

## 文件与接口

新增目录：

```text
src/update/
  CMakeLists.txt
  include/zzlogg/update/{version.h,signature.h,manifest.h,policy.h}
  src/{version.cpp,signature.cpp,strictjson.cpp,strictjson.h,manifest.cpp,policy.cpp}
tests/update/
  CMakeLists.txt
  {versiontest.cpp,signaturetest.cpp,manifesttest.cpp,policytest.cpp,fixturetooltest.cmake}
  fixtures/{rfc8032.json,README.md}
tools/update/{CMakeLists.txt,testfeed.cpp}
docs/development/UPDATE_PROTOCOL.md
```

新增离线依赖：`3rdparty/vendor/monocypher` 的两个 C 源文件、两个头文件、许可证及来源说明；`3rdparty/vendor/nlohmann-json` 的 json.hpp、许可证及来源说明。包装 CMake 在 `cmake/ZzUpdateDependencies.cmake`，不修改上游源文件。

接口统一使用 `zzlogg::update` 命名空间：

```cpp
struct Version { unsigned year, month, patch; };
std::optional<Version> parseVersion(std::string_view);
int compareVersion(Version a, Version b); // -1 / 0 / 1

bool verifyEd25519(std::string_view message,
                  const std::vector<std::uint8_t>& publicKey,
                  const std::vector<std::uint8_t>& signature);
```

`manifest.h` 定义 Artifact、Manifest、TrustedKey、AcceptedMetadata 和 VerificationContext。TrustedKey 包含 id、32 字节公钥和 Production/Test 用途；VerificationContext 包含受信键表、明确的环境、允许主机、当前时间、构建时间、当前渠道及该渠道上次接受的记录。AcceptedMetadata 为序号和原始负载 SHA-512 指纹，不使用签名字节作指纹。

`VerifiedManifest` 的构造函数私有，仅清单验证器可以创建；暴露只读字段。失败返回命名错误枚举和不含原始敏感内容的诊断，不返回可供策略层使用的普通 Manifest。

```cpp
struct VerificationResult {
    std::optional<VerifiedManifest> value;
    VerificationError error;
};
VerificationResult verifyManifest(std::string_view envelope,
                                  const VerificationContext&);
```

`policy.h` 定义 InstalledRelease（版本、发布序号、正式/开发构建、系统及架构、发布类型、系统版本、数据 schema、更新协议版本）；Decision 包含状态和成功时选中的 Artifact 副本。调用签名为 `Decision selectUpdate(const VerifiedManifest&, const InstalledRelease&)`，无网络与文件副作用。

## 任务 1：严格版本解析与比较

**文件：** 创建 version.h/.cpp、tests/update/versiontest.cpp 和两个 CMakeLists；修改 src/CMakeLists.txt、根 CMakeLists.txt。

- [ ] 写数据驱动测试，包括下述断言；测试放入现有 UI 测试开关下的 tests/update 子目录，注册为 `zzlogg_update.version`。

```cpp
QVERIFY(parseVersion("26.09.00").has_value());
QVERIFY(!parseVersion("26.9.00"));
QVERIFY(!parseVersion("26.13.00"));
QVERIFY(!parseVersion("26.09.00-dev"));
QCOMPARE(compareVersion(*parseVersion("26.09.99"),
                        *parseVersion("26.10.00")), -1);
QCOMPARE(compareVersion(*parseVersion("26.12.00"),
                        *parseVersion("27.01.00")), -1);
```

- [ ] 先建立返回解析失败的最小可编译实现，运行 `zzlogg_update_version_test.exe -o -,txt`，确认合法版本断言失败；不把头文件缺失当成有效红灯。
- [ ] 实现固定 8 字节格式及 ASCII 数字检查，再构造三段数值；用 tuple 比较，不使用 Qt 或浮点数。增加空串、空白、全角数字、尾随换行、相等版本测试。
- [ ] 构建 `cmake --build out/ui-vs --config Release --target zzlogg_update_version_test --parallel 8`；运行 `ctest --test-dir out/ui-vs -C Release -R "^zzlogg_update.version$" --output-on-failure`。
- [ ] 验证失败案例均拒绝后提交：`feat: 建立更新版本解析与比较规则`，正文说明日期式版本及拒绝开发后缀。

## 任务 2：离线 Ed25519 验签

**文件：** 创建 signature.h/.cpp、signaturetest.cpp、fixtures/rfc8032.json、依赖包装及 Monocypher 离线源；修改 src/update/CMakeLists.txt、NOTICE、cmake/StageRuntimeLicenses.cmake。

- [ ] 从官方 4.0.3 发布包获取源码，对照官方 SHA-512 校验，记录完整下载 URL、版本、包摘要及各保留文件摘要。只保存核心与 optional/monocypher-ed25519 文件及许可证。
- [ ] RFC 8032 向量至少覆盖空消息与单字节消息。先让 verifyEd25519 返回 false，运行 `zzlogg_update_signature_test` 观察正确签名被拒绝的红灯。

```cpp
// RFC 8032 test 1: empty message.
const auto pk = QByteArray::fromHex(
    "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
const auto sig = QByteArray::fromHex(
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
    "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
QVERIFY(verifyEd25519({}, {pk.begin(), pk.end()}, {sig.begin(), sig.end()}));
```

- [ ] 实现长度先验：公钥必须 32 字节，签名必须 64 字节；仅调用 `crypto_ed25519_check`，返回值等于 0 才接受。不得使用基于 BLAKE2b 的普通 EdDSA 接口冒充 Ed25519。
- [ ] 增加消息/签名/公钥各一位变化、截断、全零签名及非法长度测试。签名测试不能只使用同一库现场生成的签名作为正确性依据。
- [ ] 注册 `zzlogg_update.signature`，运行对应 CTest；检查目标依赖不含 Qt DLL，许可证部署测试仍通过。提交：`feat: 接入离线静态 Ed25519 验签`。

## 任务 3：严格 JSON 和签名封装验证

**文件：** 创建 strictjson.h/.cpp、manifest.h/.cpp、manifesttest.cpp；引入 nlohmann-json 离线源及来源说明，更新 NOTICE 和 cmake/StageRuntimeLicenses.cmake。

- [ ] 获取 3.12.0 官方 json.hpp；发布页公布的 SHA-256 为 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`。摘要不一致立即停止，不能跳过检查。
- [ ] 首先测试封装重复 schema、转义等价 keyId、嵌套重复键、深度超限、尾随 JSON、非规范 Base64；以“简单 DOM 解析会接受重复键”为变异目标，观察测试失败。
- [ ] strictjson 使用 nlohmann 的解析回调跟踪每个对象的已解码键集合，遇重复或深度超限立即抛出内部解析拒绝异常；顶层捕获并转为结构错误。不以回调返回 false 实现拒绝，因为 false 可能仅丢弃字段并继续返回成功。
- [ ] Base64 使用标准字母表、长度与填充位检查，并验证重编码结果完全一致。限制长度发生在分配大缓冲区之前。
- [ ] 构造带 domain/keyId 的签名输入，先查找用途正确的可信公钥，再验签，最后调用负载解析。空生产键表、Test 用途键、未知 key ID 在 Production 上下文全部拒绝。内置公开 fixture 公钥的拒绝列表；即使误将 fixture 公钥标注为 Production，也必须拒绝。生产上下文只由编译配置构造，不向应用用户暴露切换信任环境的参数。
- [ ] 对已签名原始负载增加一个空格但复用旧签名必须失败；重新签署语义相同但字节不同的负载可以验签，但受到元数据序号冲突规则约束。
- [ ] 注册 `zzlogg_update.manifest`，运行正确封装及所有拒绝案例后提交：`feat: 严格验证更新清单签名封装`。

## 任务 4：负载约束、反回放与发布选择

**文件：** 完成 manifest.h/.cpp，创建 policy.h/.cpp、policytest.cpp。

- [ ] 增加真实签名 fixture：注入 now=1800000000、issuedAt=1799999900、expiresAt=1800003600、metadataSequence="2"、releaseSequence="2"、version="26.10.00"，平台 windows/x64、portable、zip，大小 "1024"，测试主机 `updates.example.invalid`。该域名不进行网络访问。
- [ ] 建立生成 fixture 的纯测试 helper：采用测试种子产生完整符合上表的 JSON 和签名。测试类使用 VerificationContext::Test 环境与专用公钥；此 helper 不进入 src/update。
- [ ] 表驱动测试：到期边界、未来签发、过长有效期、低序号、相同序号相同负载、相同序号不同负载、uint64 溢出、负数/小数、JSON 数字代替序号字符串、错误产品及无对应架构。
- [ ] 测试选择行为，而非仅检查字段存在：

```cpp
// verified 由上述已签名 fixture 验证得到；installed 为明确的旧发布。
InstalledRelease installed = fixtureInstalled("26.09.00", 1, "portable");
QCOMPARE(selectUpdate(verified, installed).status, DecisionStatus::Available);
installed.distribution = Distribution::Installer;
QCOMPARE(selectUpdate(verified, installed).status, DecisionStatus::NoCompatibleArtifact);
installed.developmentBuild = true;
QCOMPARE(selectUpdate(verified, installed).status, DecisionStatus::DevelopmentBuild);
```

- [ ] fixtureInstalled 定义在 policytest.cpp，仅创建固定 windows/x64、系统 10.0.22621、schema=1、协议=1 的测试输入；不调用被测选择函数推导期望值。
- [ ] 策略按渠道、正式构建、更新协议、数据格式、发布序号/版本、系统/架构/类型、最低 OS 顺序作判断；结构错误与“无新版”使用不同状态。
- [ ] 验证元数据接受记录只在验证成功时存在，拒绝结果不能让调用方推进序号。首次接受也不能绕过期限、公钥用途或产品检查。
- [ ] URL 测试覆盖 http、用户名密码、片段、CR/LF、反斜杠、非法转义、非允许主机和端口、主机后缀欺骗；本阶段验证地址，不执行下载。
- [ ] 注册 `zzlogg_update.policy` 并运行全部更新测试。提交：`feat: 实现更新清单策略与反回放检查`。

## 任务 5：隔离测试清单工具与端到端验收

**文件：** 创建 tools/update/testfeed.cpp、tools/update/CMakeLists.txt、fixturetooltest.cmake 和 docs/development/UPDATE_PROTOCOL.md；修改根 CMakeLists.txt。

- [ ] 新增默认关闭的 `ZZLOGG_BUILD_UPDATE_TEST_TOOLS` 选项；只有开启更新测试时才允许启用。工具名 `zzlogg_update_testfeed`，不安装、不放入 runtime，不处理生产私钥。
- [ ] 工具只支持 `generate --output <new-file>` 和 `verify --input <file> --now <unix-seconds>`；generate 使用公开测试种子、固定隔离域名，拒绝覆盖已有输出。生成清单的 issuedAt/expiresAt 与任务 4 一致；用途仅为可复现 fixture，不作为生产发布工具。
- [ ] CMake 集成测试生成清单，用工具 verify 检验退出码为 0；将签名最后一个字符修改后再次验证必须非 0；Production 上下文拒绝同一文件。测试输出只能位于构建树的临时测试目录。
- [ ] 文档写明字节级签名输入、字段表、拒绝规则、已见序号持久化责任和引导版本限制；说明真实发布工具及密钥管理属于上线阶段。
- [ ] 运行 `cmake --build out/ui-vs --config Release --parallel 8`，然后 `ctest --test-dir out/ui-vs -C Release --output-on-failure`。核对旧 64 项及新增测试逐项结果，不只报告新测试。
- [ ] 对新验证核心进行只读安全审查，修复重要反馈再提交：`test: 补齐更新协议端到端验证与使用说明`。

## 完成标准与下一阶段

第一阶段完成意味着：可信输入可被解析并选出正确载荷，不可信或不适用输入得到可解释的拒绝，测试工具可以离线复现；不意味着主程序已经可以在线更新。

下一阶段才接入 Qt 网络与 UI，并负责持久化 AcceptedMetadata、校验下载最终地址及包哈希。本阶段没有包内文件路径验证、解压和安装事务，这些归属便携/安装执行阶段，不允许被宣传为已经受保护。

## 参考与实施前复核

- [Monocypher 下载](https://monocypher.org/download/)：锁定 4.0.3。
- [Monocypher 已知问题](https://monocypher.org/bugs)：旧版签名时序问题是不能直接选 4.0.2 的原因。
- [Ed25519 API](https://monocypher.org/manual/ed25519)：检查成功返回 0，调用方必须验证缓冲区长度。
- [nlohmann/json 3.12.0 发布](https://github.com/nlohmann/json/releases/tag/v3.12.0)：校验 vendored 文件。
- [JSON 解析回调](https://json.nlohmann.me/features/parsing/parser_callbacks/)：拒绝重复键而不是悄悄覆盖。

执行前再次核对两个依赖的安全公告；若发现新的相关漏洞，停止引入该固定版本并调整依赖方案，不自行修改密码学实现。
