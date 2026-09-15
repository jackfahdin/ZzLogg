# 安装版更新 3A：安装身份基础实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 统一安装登记并实现只读身份探测，为安装版更新建立可测试的前置条件，不启用安装执行。

**架构：** NSIS 负责写入明确的安装登记；Qt 适配层将只读探测与纯策略分离。安装身份匹配只是部署一致性，不授予提权或载荷执行权限。

**技术栈：** C++17、Qt 6 Core/Test、Windows 文件/注册表 API、NSIS、CMake/CTest。

**规格：** `docs/superpowers/specs/2026-09-15-installer-only-update-design.md`。本计划仅覆盖其中 3A；3B、3C 与正式发布不属于本计划完成声明。

## 全局约束

- 仅 Windows x64 安装版提供在线更新；便携版继续手动更新。
- 不实现安装器启动、UAC、退出交接、文件替换、恢复或新的安装按钮。
- 安装登记唯一来源为 `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg` 的 64 位视图；路径值为 `InstallLocation`，引导字段为 DWORD `UpdateIdentitySchema=1`。
- 测试不写真实 HKLM，不运行真实安装器，不接触用户数据；Windows 路径测试只能操作 QTemporaryDir 中自己创建的文件。
- `.zzlogg-install-root` 只是身份证据，不能作为安全授权；缺发布身份时服务继续使用空身份，不伪造 InstalledRelease。
- 不删除 portable 协议枚举，不修改数据存储策略，不自动迁移历史注册表键。
- 中文提交标题，下方详细中文说明实现、测试与未开放边界；不自动合并或推送。

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `packaging/windows/ZzLogg.nsi` | 安装目录回读、64 位登记视图和引导字段 |
| `tests/ui_acceptance/windowsinstallercontracttest.cmake` | NSIS 静态契约，不宣称真实安装验收 |
| `src/updateqt/include/zzlogg/updateqt/installationidentity.h` | 证据类型、只读结果与查询接口 |
| `src/updateqt/src/installationidentity.cpp` | 不接触系统的判定策略 |
| `src/updateqt/src/installationprobe.cpp` | 当前程序、注册表、标记和真实路径的只读收集 |
| `src/updateqt/src/installationprobe_p.h` | 私有注册表读取适配接口，供测试注入 |
| `tests/updateqt/installationidentitytest.cpp` | 判定矩阵 |
| `tests/updateqt/installationprobetest.cpp` | 假注册表与临时文件的收集/异常测试 |
| `src/updateqt/CMakeLists.txt`、`tests/updateqt/CMakeLists.txt` | 源文件及测试目标接线 |
| `docs/development/UPDATE_PROTOCOL.md` | 已实现范围与真实安装未验收说明 |

## 任务 1：统一安装登记契约

- [x] 在 `windowsinstallercontracttest.cmake` 增加失败断言：目录回读必须命中卸载项的 InstallLocation；x64 初始化必须选择 64 位视图；安装段写入 DWORD 引导字段；卸载初始化使用同一视图。

```cmake
foreach(required_literal IN ITEMS
  "InstallDirRegKey HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZzLogg\" \"InstallLocation\""
  "SetRegView 64"
  "\"UpdateIdentitySchema\" 1")
  string(FIND "${nsis_content}" "${required_literal}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing installer identity contract: ${required_literal}")
  endif()
endforeach()
```

将断言放在现有 `file(READ "${nsis_path}" nsis_content)` 及换行归一化之后；另外逐段检查 `.onInit`、`un.onInit` 和应用安装段，不能只通过任意注释出现的字符串放行。

- [x] 运行 `ctest --test-dir out/ui-vs -C Release -R windows_installer_contract --output-on-failure`，确认因新增登记要求失败，而不是缺少构建或输入文件。
- [x] 修改 NSIS：InstallDirRegKey 改为上述唯一键/值；在 `.onInit` 和 `un.onInit` 按 ARCH32 选择 32/64 位视图；现有安装段在 InstallLocation 旁增加字段。

```nsis
Function .onInit
!ifdef ARCH32
    SetRegView 32
!else
    SetRegView 64
!endif
FunctionEnd

; un.onInit 使用相同条件，卸载读取/删除同一视图。
; 安装段添加，沿用当前 HKLM 卸载键：
WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg" "UpdateIdentitySchema" 1
```

不要删除用户在普通全新安装中选择目录的能力，不改文件关联和数据清理语义；专用升级入口尚不存在。
- [x] 重跑契约测试并验证旧的完整 runtime 文件集/安全卸载断言仍通过。记录这不是 NSIS/UAC 实际运行测试。
- [x] 提交 `fix: 统一安装目录登记与更新身份标记`，正文列出视图、键值和静态测试结果。

## 任务 2：建立可独立测试的身份判定

- [x] 创建头文件及测试，定义以下类型；生产结果中不得出现“允许提权”或“可执行”字段。

```cpp
namespace zzlogg::updateqt {
enum class InstallationKind { Unregistered, Legacy, Registered, Invalid, Unsupported };
struct InstallationEvidence {
    bool supportedPlatform = false;
    bool readFailed = false;
    bool registrationPresent = false;
    bool safePaths = false;
    bool sameDirectory = false;
    bool markerPresent = false;
    bool markerValid = false;
    std::optional<unsigned> identitySchema;
    QString installRoot;
};
struct InstallationIdentity {
    InstallationKind kind = InstallationKind::Invalid;
    QString installRoot;
};
InstallationIdentity evaluateInstallation(const InstallationEvidence&);
InstallationIdentity probeCurrentInstallation();
}
```

包含 `<optional>` 和 `<QString>`。第一项是纯判定，第二项由任务 3 定义；任务 2 不调用未实现的探测函数。

```cpp
InstallationEvidence e;
e.supportedPlatform = true;
e.registrationPresent = true;
e.safePaths = e.sameDirectory = e.markerPresent = e.markerValid = true;
e.identitySchema = 1;
e.installRoot = QStringLiteral("C:/Program Files/ZzLogg");
QCOMPARE(evaluateInstallation(e).kind, InstallationKind::Registered);
e.sameDirectory = false;
QCOMPARE(evaluateInstallation(e).kind, InstallationKind::Invalid);
```

- [x] 注册 `zzlogg_update_qt_test(installation_identity installationidentitytest.cpp)`，先运行新增测试确认缺实现导致失败；不要把构建环境失败当作功能红灯。
- [x] 实现判定优先级：Unsupported → readFailed 时 Invalid → 无登记且无标记时 Unregistered → 无登记但有标记时 Invalid → 路径/目录/标记不一致时 Invalid → 缺 schema 时 Legacy → schema 非 1 时 Invalid → Registered。只有 Registered 返回非空 installRoot，其余结果路径清空。
- [x] 参数化覆盖：无登记便携、复制安装目录、读取拒绝、缺失/畸形标记、路径不一致、不安全路径、缺/未知 schema、不支持平台、正常注册；证明任一条件不能由其他条件短路为成功。
- [x] 构建 `zzlogg_update_installation_identity_test`，执行 `ctest --test-dir out/ui-vs -C Release -R '^zzlogg_update.installation_identity$' --output-on-failure`；失败用例恢复实现后全绿。
- [x] 提交 `feat: 新增安装身份只读判定模型`，说明 Registered 不是更新执行授权。

## 任务 3：接入只读 Windows 探测与隔离验证

- [x] 在私有头中定义可注入的登记读取边界；默认生产入口内部构造真实读取器，不提供命令行/设置注入。

```cpp
struct InstallationRegistration {
    bool present = false, readFailed = false;
    QString location;
    std::optional<unsigned> schema;
};
class InstallationRegistryReader {
public:
    virtual ~InstallationRegistryReader() = default;
    virtual InstallationRegistration readMachine64() const = 0;
};
InstallationIdentity probeInstallation(const QString& executablePath,
                                      const InstallationRegistryReader&);
```

这些类型放入 `zzlogg::updateqt`，仅从私有头导出。Qt 测试创建 FakeRegistry 子类与 QTemporaryDir，构造 ZzLogg.exe 和 `.zzlogg-install-root`，不需要真实可执行载荷。

- [x] 先写失败测试：登记目录匹配且正常标记为 Registered；相同目录大小写变化仍匹配；前缀近似目录、复制目录、缺文件、登记读取拒绝、标记超限/非法文本均不能 Registered。临时目录写文件使用 QSaveFile/QFile，禁止改真实安装文件。
- [x] 实现 Windows 注册表读取：使用 `RegOpenKeyExW` 的 `KEY_READ | KEY_WOW64_64KEY` 与 `RegQueryValueExW`；不存在与拒绝读取分开。InstallLocation 接受绝对的 REG_SZ 或经系统展开的 REG_EXPAND_SZ，拒绝空值、相对路径和非字符串；schema 只接受精确 DWORD，畸形类型返回 readFailed。全部关闭句柄。
- [x] 实现文件证据：使用 QCoreApplication::applicationFilePath 获取真实入口，逐个检查祖先、根、主程序与标记的文件属性；任何 FILE_ATTRIBUTE_REPARSE_POINT 拒绝。正常目录经系统规范化后比较，禁止仅用字符串前缀比较，禁止先跟随链接再判断。
- [x] 标记限制为 256 字节，格式为现有 `ZzLogg <版本>\r\n` 或 LF；版本允许现有非空可打印构建文本，拒绝 NUL、多行和控制字符。标记版本不成为正式发布版本来源。标记只能是普通文件，读取错误返回 Invalid。
- [x] 非 Windows x64 的生产入口直接返回 Unsupported，平台代码通过条件编译隔离。CMake 在 WIN32 下私有链接 Advapi32，不引入运行时 DLL 或 Qt Widgets 依赖。
- [x] 增加真实 Windows 临时目录 junction 测试，覆盖根、祖先、主程序/标记链接；创建失败明确记录环境受限，不能写成通过。测试清理只删除自己创建的链接，不遍历链接目标。
- [x] 注册 `zzlogg_update_qt_test(installation_probe installationprobetest.cpp)`，运行两个新测试及全套 CTest；保留 `kloggapp.h` 当前空发布身份，不接线自动安装。

```powershell
$env:CL = '/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
```

- [x] 更新 `UPDATE_PROTOCOL.md`，写明探测实现和测试证据、生产安装仍关闭、未运行真实 NSIS/UAC。检查无新增安装动作、无注册表写入测试。
- [x] 提交 `feat: 实现 Windows 安装身份探测与隔离回归`，正文记录测试数量和边界。

## 自检与交付

3A 的登记一致性由任务 1 覆盖；身份矩阵由任务 2 覆盖；真实读取、路径证据和无系统写入由任务 3 覆盖。只有这三项全部完成才能声明安装身份基础完成，不能声明在线升级完成。

下一份计划为 3B 发布身份与安全交接：在本期接口落地后细化编译身份、代码签名/文件稳定性、退出准备和进程握手。3C 的受保护文件事务及隔离系统验收仍是开启实际安装按钮的必要门禁。

## 执行记录

- 开始执行时分支为 `codex/installer-update-plan`，基线 `7d85a139`，隔离工作树 `.worktrees/update-core`；基线 CTest 79/79 通过（53.55 秒）。
- 任务 1：`5a119221`、`b2fa9ca0`。登记契约测试完成红绿验证及注释、错误视图、错误值名和清理顺序变异检查；最终聚焦测试 1/1 通过（5.13 秒），独立审查及一轮定向修复复审通过。
- 任务 2：`3e25cb31`。纯判定 14 项 QtTest 通过；未知 schema 被误判为 Legacy 的变异被捕获，独立审查通过。
- 任务 3：`041c671e`。探测 72 项 QtTest 通过，2 项文件符号链接测试因当前环境权限限制跳过；根/祖先 junction 和 NTFS 大小写敏感目录实际通过。原始登记解析、标记大小和祖先重解析点的变异检查有效，独立审查通过。
- 主控在 `041c671e` 上独立完成 Release 全量构建及 CTest，81/81 通过（53.35 秒）；判定 14 项通过、探测 72 项通过和 2 项跳过再次核实。测试数包含初始化/清理，不将跳过项计为行为验收通过。
- 整阶段审查发现默认流式 UTF-8 解码器可能暂存不完整后缀或吞掉版本 BOM；`a1f9616a` 改为 Stateless + ConvertInitialBom 并拒绝空解码文本。四个新增字节回归先复现错误、修复后通过，探测增加至 76 项通过、2 项跳过；定向复审确认问题解决且无新阻断项。
- 最终修复后的主控验证：完整 Release 构建退出码 0，CTest 81/81 通过（53.97 秒）。日志位于 `out/ui-vs/identity-final-build.log`、`out/ui-vs/identity-final-tests.log`；最终源代码为 `a1f9616a`，此后仅补充本验收记录。
- 本期没有更改 `src/app`、`src/ui` 或 `src/update` 的接线/协议，没有执行真实安装器或写入本机注册表，也没有进行跨平台实际运行测试。

执行中的必要补充：NSIS 官方说明 SetRegView 不影响 InstallDirRegKey，所以需要在 `.onInit` 选择视图后显式 ReadRegStr，并保留 `/D=` 优先级；代价是新增初始化和参数处理，仍需真实安装器验收。审查同时修正了旧 Software/ZzLogg 键的清理视图，保持原行为。

本期按计划保留 NSIS 静态契约检查，不能把其通过当作 NSIS 二进制或 UAC 的运行验证；静态检查无法证明的运行时行为由安装执行关闭和正式上线门禁约束。

路径范围补充：只支持普通本地盘符绝对路径，原始 UNC、设备路径、相对路径和有歧义的路径分量直接拒绝，防止规范化抹去风险证据；代价是这些非标准安装位置继续手动更新。目录句柄身份比较补足大小写敏感 NTFS 目录的真实身份核对，不把字符串相等当成唯一依据。

两个文件符号链接测试仍需在允许创建链接的隔离 Windows 环境补验；真实 NSIS 编译/安装、UAC、正式更新源、签名证书和升级恢复均未验收。下一阶段为 3B 发布身份与安全交接，本期没有启用自动安装。
