# 安装更新 3B.2：执行前验证实现计划

> **面向 AI 代理的工作者：** 使用 subagent-driven-development 逐任务实现；使用复选框记录进度。

**目标：** 保留已验签的原始信封和选择记录，提供重新验证清单及 Windows 安装包的基础，不启动任何程序。

**架构：** 无 Qt 核心保留原始字节并重新选择目标；Qt 下载快照携带非授权性质的选择记录。Windows 原生静态库持有稳定文件及祖先目录句柄，重新核对大小、SHA-256、Authenticode 和发布者证书策略；生产策略缺失时拒绝，不存在跳过验证的入口。

**技术栈：** C++17、现有 Ed25519/JSON 核心、Qt6 Test、Windows 文件 API、BCrypt/Crypt32/Wintrust、CMake。

**规格：** `docs/superpowers/specs/2026-09-16-installer-handoff-design.md` 第 3、6 节。

## 全局约束

- 仅 Windows x64 安装版、全量 NSIS 包、用户主动更新、生产入口默认关闭。
- 不修改 `src/app`、`src/ui`、安装脚本或系统信任库；不调用安装器、UAC 或进程启动 API，不写 HKLM。
- 普通权限验证不能替代管理员阶段的独立验证；用户可写缓存及交接消息均不是管理员信任根。
- 生产证书策略为空并拒绝执行；测试替身只进入测试目标，禁止通过命令行、环境变量或用户配置开启后门。
- 保留原始签名信封，不重新序列化 payload。新的验证不得信任旧的 verifiedPath 或缓存成功状态。
- 测试仅操作自己创建的隔离临时目录。跨平台真实执行不在本轮范围；不把受控目标描述称为跨平台测试。
- 每个任务先红灯再实现，提交使用中文标题和详细中文正文。完整构建/全套 CTest 在任务提交前运行一次。

## 文件结构

- `src/update/include/zzlogg/update/manifest.h`、`src/update/src/payload.cpp`：VerifiedManifest 保存原始信封。
- `src/update/include/zzlogg/update/executionselection.h`、`src/update/src/executionselection.cpp`：选择记录与纯重验函数。
- `src/updateqt/include/zzlogg/updateqt/updatedownloadservice.h`、对应 cpp：下载完成提供选择记录，所有无效化路径清空。
- `tests/update/executionselectiontest.cpp`、现有 manifest/service/download 测试：真实签名、状态与生命周期回归。
- `src/update/include/zzlogg/update/packageverification.h`：无 Qt、不可复制的稳定文件租约和验证结果。
- `src/update/src/stablepackage_win.cpp`、`authenticode_win.cpp`、`packageverification.cpp`、私有头：平台文件锁定、证书验证和组合门禁，职责分开。
- `tests/update/packageverificationtest.cpp`、Windows 测试私有支持文件：真实文件替换/共享、哈希、拒绝路径及仅测试目标的信任边界替身。
- `src/update/CMakeLists.txt`、`tests/update/CMakeLists.txt`、`docs/development/UPDATE_PROTOCOL.md`：注册与文档。

## 任务 1：保留信封并重新验证目标选择

**文件：** 上述 manifest、executionselection、Qt 下载服务、对应测试和 CMake。

- [ ] 先添加行为测试。VerifiedManifest 新 getter `signedEnvelope()` 返回验证输入的逐字节副本；故意带外层空白的合法 JSON 必须原样保存，输入销毁后仍可用。失败验证不能产生对象。
- [ ] 建立以下接口的失败测试并运行：

```cpp
namespace zzlogg::update {
struct UpdateSelection {
    std::string signedEnvelope;
    AcceptedMetadata accepted;
    std::uint64_t releaseSequence=0;
    Artifact artifact;
};
std::optional<UpdateSelection> makeUpdateSelection(
    const VerifiedManifest&, const InstalledRelease&);
enum class SelectionError { None, StateMissing, VerificationFailed,
    TargetChanged, Unavailable };
struct SelectionResult {
    std::optional<Artifact> artifact;
    SelectionError error=SelectionError::Unavailable;
};
SelectionResult revalidateUpdateSelection(const UpdateSelection&,
    const VerificationContext& freshContext, const InstalledRelease& current);
}
```

测试 fixture 使用现有 fixturehelper，改 artifact 为 installer/nsis-exe，当前正式 windows/x64、序号 1、版本 26.09.00、schema 1、OS 10.0.22631。外层信封包含换行空白。明确独立期望：选择目标序号 2，重验成功返回签名包的精确 URL/size/hash。

```cpp
auto context=update_fixture::context();
auto verified=verifyManifest(envelope,context);
auto selection=makeUpdateSelection(*verified.value,installed);
context.lastAccepted=verified.value->acceptedMetadata();
QVERIFY(revalidateUpdateSelection(*selection,context,installed).artifact);
context.now=1800003600;
QCOMPARE(revalidateUpdateSelection(*selection,context,installed).error,
         SelectionError::VerificationFailed);
```

- [ ] 覆盖：信封篡改、过期、渠道切换、撤销签名 key、Test→Production、反回放状态缺失/更高/同序号冲突、目标记录的序号/digest/releaseSequence/全部 artifact 字段被改、当前已升级/开发版/便携版/错误平台/零序号/无效版本或 OS，以及原始合法成功。逐字段匹配包含 minOsVersion，不能只比较 URL/hash。
- [ ] 最小实现：只在 verifyManifest 成功最后复制输入；makeUpdateSelection 只为有效 windows/x64 正式 Installer 且 Available 的 nsis-exe 目标生成记录。revalidate 必须用调用方最新 keys/now/channel/lastAccepted 重新验原始信封，lastAccepted 缺失拒绝；重新调用 selectUpdate，核对 accepted、releaseSequence、全部 artifact 字段，任何差异拒绝。输入 record 不视为可信类型。保留底层时效规则，不自行放宽。
- [ ] DownloadSnapshot 末尾新增 `std::optional<update::UpdateSelection> selection`，避免破坏旧聚合初始化。Verified 成功时用原 release_ 和 installed_ 生成；错误、取消、切换及 invalidate 清空；既有 verifiedPath 保留语义但不授权。CheckSnapshot 内 VerifiedManifest 已携带信封，无需重复字段。保留现有下载范围和 UI 行为，只有合格 Installer 才有 selection。
- [ ] 测试真实 UpdateService→下载快照携带原字节，验证无效化清空；不得依赖旧对象指针寿命。聚焦 manifest/service/download_service/execution_selection 后跑完整构建与 CTest，记录红绿证据及一项移除记录比较后的变异失败，再恢复。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_update\.(manifest|execution_selection|service|download_service)$' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 保留签名信封并重验安装目标（3B.2 任务一）" -m "记录原始信封、最新状态校验和下载选择记录的失效规则。"
```

## 任务 2：稳定文件、Authenticode 与生产关闭的组合验证

**文件：** 上述 packageverification、stablepackage_win、authenticode_win、私有头、测试、CMake 和 UPDATE_PROTOCOL。

- [ ] 编写失败测试和接口。独立静态目标 `zzlogg_update_execution`，依赖核心与 Windows 系统库，不依赖 Qt；非 Windows 编译拒绝实现。不修改核心其他消费者的链接策略。使用 Pimpl 封装句柄，租约移动后原对象失效，析构释放所有句柄，不暴露可释放的原始句柄给调用者。

```cpp
namespace zzlogg::update {
enum class PackageVerificationError { None, Unsupported, SelectionRejected,
    PublisherPolicyMissing, InvalidPath, FileUnavailable, SizeMismatch,
    HashMismatch, SignatureUntrusted, PublisherMismatch };
class VerifiedPackage { // private construction, move-only, Pimpl
public:
    ~VerifiedPackage();
    VerifiedPackage(VerifiedPackage&&) noexcept;
    VerifiedPackage& operator=(VerifiedPackage&&) noexcept;
    const std::wstring& path() const; // only meaningful on live result
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    // Only internal verification factory can construct.
};
struct PackageVerificationResult {
    std::optional<VerifiedPackage> package;
    PackageVerificationError error=PackageVerificationError::Unsupported;
};
PackageVerificationResult verifyPackageForExecution(const UpdateSelection&,
    const VerificationContext&, const InstalledRelease&, const std::wstring& path);
}
```

上述 API 仅接收当前可信状态，不自行读取用户注册表。成功对象不是管理员授权；本阶段不实现 launch。

- [ ] 明确生产组合顺序：重新选择验证且只允许 Production trust → 内置发布者策略（当前为空，返回 PublisherPolicyMissing）→ 稳定打开文件 → 大小/流式 SHA-256 → Authenticode 与发布者匹配 → 再次确认句柄/路径身份 → 移动句柄进入结果。任何失败关闭已获得资源。缺生产策略不能用清单提供的证书代替。
- [ ] 稳定打开：只接受标准绝对本地盘符路径，拒绝 UNC、设备命名空间、相对路径、NUL、ADS、点段、空组件、尾随点/空格及 DOS 设备名；只接受固定本地盘。从根到父目录逐一打开且保持不共享删除的目录句柄，使用 OPEN_REPARSE_POINT/BACKUP_SEMANTICS，检查每级无 reparse。文件使用 GENERIC_READ、FILE_SHARE_READ、OPEN_EXISTING、OPEN_REPARSE_POINT，拒绝目录/reparse/多硬链接，GetFileType 必须 DISK；有现存写/删句柄则拒绝。核对最终句柄解析路径和身份，不依靠存在性检查。
- [ ] 同一稳定文件句柄上读大小并用 BCrypt SHA-256 流式哈希（不一次读完整包），预期 size 必须 1..512MiB、sha256 为规范小写 64 hex；读错误/多读/少读拒绝。大小和哈希匹配只是内容检查，不绕过签名。
- [ ] Authenticode：WinVerifyTrust WINTRUST_ACTION_GENERIC_VERIFY_V2，WTD_UI_NONE，WINTRUST_FILE_INFO 同时传稳定完整路径及已有读句柄；仅 LONG==0 成功，关闭所有 state。不忽略撤销状态（链检查排除根），未知/离线无法确认时失败。从这次已验证的 provider state 取主签名者叶证书 DER SHA-256，与内置允许列表逐字节比较；不比较显示名称、不重新从其他路径取证书、不接受时间戳签名者替代。Wintrust helper 动态解析时仅加载 System32，缺函数拒绝。
- [ ] 测试真实目录/文件：正确字节 `abc`，SHA-256 使用独立常量 `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`；错大小、错哈希、打开时已有写句柄、持有租约时写入/删除/重命名文件及父目录失败、释放后可以操作、移动所有权后仍受保护。通过私有底层锁定接口测试，不让假的可信回调生成生产 VerifiedPackage。
- [ ] 测试 junction/符号链接/硬链接及祖先替换，符号链接创建权限不足只记录明确 skip；junction 和共享冲突不得统称跳过。私有 Authenticode 算法边界可使用仅编译进测试目标的 WinTrust API 替身验证成功/错误返回、主签名者指纹匹配/不匹配和资源关闭；真实 Windows 无签名文件必须拒绝。不能把替身成功当作真实证书验收，也不能写信任库或生成安装证书。
- [ ] 组合测试证明 Test 清单、缺策略、过期/篡改/回放/改变目标不产生 VerifiedPackage；缺正式证书时不伪造生产成功。文档列清真实签名链成功、证书轮换/撤销、管理员重验及实际启动稳定性仍是上线前验收门槛，不宣称本阶段具备安装能力。
- [ ] 聚焦测试先红后绿，移除文件共享保护做一次变异测试再恢复；跑完整 Release 构建和全套 CTest。检查 execution 静态目标不依赖 Qt，不启动任何程序。更新协议、提交。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R 'zzlogg_update\.(execution_selection|package_verification)$' --output-on-failure
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
git commit -m "feat: 增加安装包执行前安全验证（3B.2 任务二）" -m "保持文件和路径句柄，核对大小、哈希与 Authenticode 发布者，缺生产策略时关闭执行资格。"
```

## 验收与下一步

- [ ] 两任务审查通过，主控独立构建和 CTest，确认 master 合并结果及开发分支提交状态。
- [ ] 汇总真实测试、权限跳过、生产证书缺失边界，不自动合并本阶段成果。
- 下一步 3B.3：独立 ZzLoggUpdate.exe、身份受限本地通道和安装器交接协议；真实管理员安装事务仍属于 3C。

## Windows API 参考

- [CreateFile 共享规则与 reparse 行为](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
- [WinVerifyTrust 返回值与验证状态](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)
- [WINTRUST_FILE_INFO 已打开文件句柄](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/ns-wintrust-wintrust_file_info)
- [主签名者与 countersigner 区别](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-wthelpergetprovsignerfromchain)
