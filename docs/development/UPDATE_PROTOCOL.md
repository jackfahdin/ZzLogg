# 更新协议与离线验证核心

## 当前完成范围

本阶段提供静态 C++ 验证库：版本解析、Ed25519 验签、严格清单解析、防回放和发布载荷选择。核心不依赖 Qt，没有网络、注册表、用户数据或安装目录写入。

**这不代表主程序已经能在线更新。** 检查更新 UI、下载、状态持久化、便携版替换、安装器执行和失败回滚属于后续阶段。只支持全量包，尚无增量包执行方案。

## 代码入口

| 位置 | 职责 |
| --- | --- |
| src/update/include/zzlogg/update/version.h | YY.MM.PP 严格解析与数值比较 |
| src/update/include/zzlogg/update/signature.h | Ed25519 原始消息验签 |
| src/update/include/zzlogg/update/manifest.h | 信任上下文、只读已验证清单、接受记录 |
| src/update/include/zzlogg/update/policy.h | 当前安装信息与发布选择结果 |
| src/update/src/strictjson.cpp | JSON 重复键、深度、编码与规范 Base64 |
| src/update/src/manifest.cpp | 内部签名封装验证，不授予安装资格 |
| src/update/src/payload.cpp | 完整负载、期限、域名与防回放 |
| src/update/src/policy.cpp | 版本、系统、架构、便携/安装版及数据格式选择 |
| tests/update | 标准向量、真实签名和错误边界测试 |
| tools/update | 默认关闭的离线测试清单工具 |

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

发布说明是文本数据，后续 UI 必须按纯文本显示，不得直接交给 HTML 或富文本解释器。

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

本阶段仅校验签名清单内的 URL。后续下载器必须对**每次重定向及最终地址**重新检查，并限制下载大小、校验完整包 SHA-256。清单通过不代表包已经安全下载或安装。

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

Windows Release 完整构建通过，CTest 76/76 通过，更新界面测试内部 13 项通过。另已运行 Windows 原生 150% Qt 缩放下三语言、双主题长说明布局验证。跨平台实际运行未验证，不宣称已通过。

Qt 网络获取、渠道状态存储、统一检查服务、检查窗口和应用接线已完成。帮助菜单与设置页共用一个服务和检查窗口；设置中的渠道及自动检查选项在应用后生效。关闭检查窗口或销毁其父窗口会取消活动手动检查，迟到响应不会重新弹窗。

生产地址与公钥尚未提供，当前程序显示“更新服务尚未配置”，不发网络请求；真实生产 HTTPS 互通尚未验证。测试信任仅存在独立测试程序，不部署到运行目录。

本阶段没有包下载、解压路径检查、文件替换事务、安装器提权、回滚或发布签名工具。下一阶段实现下载进度、取消、专用缓存与 SHA-256 校验，再实现便携版与安装版执行器，最后建设真实发布流水线。

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
