# 安装版更新 3B.1：构建发布身份实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development 或 executing-plans 逐任务实现此计划。步骤使用复选框语法跟踪进度。

**目标：** 增加显式、默认关闭的构建发布身份，并提供与只读安装身份组合的纯接口，不开放安装或下载行为。

**架构：** CMake 验证正式发布输入并生成头文件；无 Qt 的核心读取编译元数据；Qt 适配层将其与 InstallationIdentity 组合为可用于更新选择的 InstalledRelease。

**技术栈：** C++17、Qt 6 Test、CMake/CTest、Windows x64。

**规格：** `docs/superpowers/specs/2026-09-16-installer-handoff-design.md` 第 2 节；整体边界继承 `2026-09-15-installer-only-update-design.md`。

## 全局约束

- 只实现 3B.1。主程序继续传入空发布身份，不创建安装按钮、不运行安装器、不写真实 HKLM。
- 默认开发构建不产生正式身份；不得伪造真实发布序号、证书、公钥或服务端配置。
- 序号 1–18446744073709551615，schema 0–4294967295，都是无符号规范十进制，不接受前导零（单独 0 除外）、空白、符号、小数或指数。
- 正式渠道仅 stable/preview，版本为 YY.MM.PP，月份 01–12；来源为既有 ZZLOGG_DISPLAY_VERSION。
- 不修改用户图片、其他工作树或 master。按任务提交中文标题及详细中文正文，不推送、不合并。

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `cmake/ZzReleaseIdentity.cmake`、`cmake/zzlogg_release_identity.h.in` | 输入验证和编译常量生成 |
| `src/update/include/zzlogg/update/releaseidentity.h`、`src/update/src/releaseidentity.cpp` | 无 Qt 的发布身份与读取 |
| `src/update/CMakeLists.txt` | 生成头与源文件接线 |
| `tests/update/releaseidentitytest.cpp`、`tests/update/releaseconfigurationtest.cmake`、`tests/update/CMakeLists.txt` | 编译读取与配置失败矩阵 |
| `src/updateqt/include/zzlogg/updateqt/installedrelease.h`、`src/updateqt/src/installedrelease.cpp` | 纯身份组合，不探测系统 |
| `tests/updateqt/installedreleasetest.cpp`、两个 updateqt CMakeLists | 身份组合矩阵与目标注册 |
| `docs/development/UPDATE_PROTOCOL.md` | 当前实现范围与正式发布输入说明 |

## 任务 1：构建身份生成与无 Qt 读取

- [x] 先创建配置测试，运行真实 CMake 子进程生成头并编译一个最小无 Qt 的消费程序（独立 CMake 工程只编译 releaseidentity.cpp/version.cpp，不重建全应用）。默认构建返回空；有效 stable/preview 构建读取精确版本、序号、schema 和平台。独立测试工程必须继承当前 generator/platform/toolset/compiler，测试使用自己的二进制子目录。

接口定义：

```cpp
namespace zzlogg::update {
struct ReleaseIdentity {
    Version version{};
    std::uint64_t releaseSequence=0;
    std::string channel, os, arch;
    std::uint32_t dataSchema=0, updaterProtocol=1;
};
std::optional<ReleaseIdentity> compiledReleaseIdentity();
}
```

最小消费者应断言返回值，而不是 grep 源码。测试 fixture 版本显式为 `26.09.00`、序号 `123`、schema `0`：

```cpp
const auto r=zzlogg::update::compiledReleaseIdentity();
if (!r || r->version.year!=26 || r->version.month!=9 || r->version.patch!=0
    || r->releaseSequence!=123 || r->channel!="stable"
    || r->os!="windows" || r->arch!="x64" || r->dataSchema!=0
    || r->updaterProtocol!=1) return 1;
```

- [x] 增加正式输入无效矩阵：缺任意字段、零序号、超上限、前导零、负数、空白、指数、分号/引号注入、错误渠道、无效月份、额外版本分量、不支持目标平台/架构。调用同一生成函数，断言失败而不是创建一个可用身份。开发 OFF 加残留完整字段仍应返回空。支持最大合法序号和 schema。
- [x] 运行测试确认因缺少生成功能失败，记录命令及输出。目标架构的配置单测可以使用隔离子进程设置 CMake 目标描述变量，但不能声称在其他架构实际运行。
- [x] 添加 CMake 模块，输入名为 ZZLOGG_OFFICIAL_RELEASE（默认 OFF）、ZZLOGG_RELEASE_SEQUENCE、ZZLOGG_RELEASE_CHANNEL、ZZLOGG_RELEASE_DATA_SCHEMA（其余默认空）；生成函数 `zzlogg_configure_release_identity(output)`。从现有 ZZLOGG_DISPLAY_VERSION 取版本，正式模式校验 Windows、64 位且编译目标 x64；VS generator platform 或编译器 architecture ID 优先于宿主 processor，不能使用“指针为 8”作为充分证据。无法确认目标则拒绝正式模式。
- [x] 生成字段使用经过约束的字面量，uint64 上限用长度和等长字典序校验，不能经浮点或有符号整数比较失真；生成 ULL 字面量。正式模式同时拒绝非零 PROJECT_VERSION_TWEAK，允许 Windows 数值版本第四段 0。OFF 模式输出不可用且清空所有正式字段。核心 `compiledReleaseIdentity()` 在编译的平台非 Windows x64 时也返回空（防止仅伪造 CMake 变量）。协议版本固定 1。
- [x] 注册 `zzlogg_update.release_identity` 和 `zzlogg_update.release_configuration`；版本/序号测试应独立于开发机器当前 Git 标签。运行新测试及核心既有测试，完成一次缺失关键校验的变异检查并恢复。

```powershell
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --target zzlogg_update_release_identity_test --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release -R '^zzlogg_update\.(release_identity|release_configuration|version|policy)$' --output-on-failure
```

- [x] 提交 `feat: 增加显式构建发布身份（3B.1 任务一）`，正文说明默认关闭、输入校验和测试边界。

## 任务 2：组合安装与发布身份，保持应用门禁关闭

- [x] 先创建纯函数测试，不 mock 生产逻辑、不查询真实注册表。接口为：

```cpp
std::optional<zzlogg::update::InstalledRelease> makeInstalledRelease(
    const std::optional<zzlogg::update::ReleaseIdentity>& release,
    const zzlogg::updateqt::InstallationIdentity& installation,
    const zzlogg::update::OsVersion& osVersion);
```

声明放在 `zzlogg::updateqt`。有效 fixture 为 `{26,9,0},123,"stable","windows","x64",0,1`，安装身份 `{Registered,"C:/Program Files/ZzLogg"}`，OS `{10,0,22631}`。验证所有返回字段，尤其 Installer、developmentBuild=false 和实际传入 OS，而不是默认零版本。
- [x] 参数化覆盖空发布身份、每个非 Registered 枚举、空根、空/未知渠道、错误 OS/架构、无效版本、零序号、非 1 updaterProtocol、OS 主版本为零；全部返回空。正常 stable/preview、schema 0/最大值、UINT64_MAX 序号返回身份（C++ uint64 字段不存在可表示的越界值）。不得仅信任结构体是由构建接口提供。
- [x] 注册 `zzlogg_update.installed_release`，先运行确认缺功能失败，再实现最小字段检查与拷贝。复用既有版本规则，避免使用文件标记或用户配置推导正式身份。
- [x] 保持 `src/app/kloggapp.h` 中两个服务参数为 std::nullopt，不新增真实 OS 探测，不改 UI 或下载安装策略。更新 UPDATE_PROTOCOL，解释正式构建不等于自动安装授权，以及 3B.2–3B.4/3C 尚未完成。
- [x] 运行聚焦测试、Release 全量构建、全套 CTest；对 Registered 或平台条件做变异证明测试捕获。提交 `feat: 组合安装与发布身份供更新选择使用（3B.1 任务二）`，详细正文记录证据与未接线范围。

```powershell
$env:CL='/MP8'
& D:/SoftWare/CMake/bin/cmake.exe --build out/ui-vs --config Release --parallel 8
& D:/SoftWare/CMake/bin/ctest.exe --test-dir out/ui-vs -C Release --output-on-failure
```

## 交付与下一阶段

两项全部完成、独立审查和主控复验后交付 3B.1。不得宣称独立更新程序、UAC、自动安装或整个 3B 已完成。

下一阶段 3B.2 的计划围绕原始信封、执行前重验和 Windows 文件稳定性编写；生产签名配置缺失不阻碍纯验证与拒绝分支的开发，但阻止正式上线。

## 执行记录

- 基线 `d6c3a7b7`，隔离工作树 `.worktrees/update-core`，分支 `codex/installer-update-plan`；基线 CTest 81/81 通过（53.00 秒）。
- 任务 1：`419e7f9a`。真实配置/编译/运行矩阵及核心读取实现，红绿和零序号变异检查完成；Release 完整构建成功，CTest 83/83 通过（87.26 秒）。
- 第一轮修复 `23cd2bde`：嵌套测试改由 CTest 定位消费者并继承单配置 build type；Windows Ninja Release 实际红绿验证完成。C++ 编译防线显式排除 ARM64EC（微软宏定义说明其也定义 `_M_X64`），受控宏 fixture 红绿通过，不声称在 ARM 机器执行。VS 配置测试 1/1 通过（35.80 秒）。
- 第二轮修复 `f707c8a0`：测试不再递归删除调用者 TEST_ROOT，只在其下创建随机唯一运行子目录；哨兵文件回归先失败后通过，配置测试 1/1 通过（35.56 秒）。独立脚本最低版本声明对齐应用实际要求 CMake 3.23。
- 任务 1 独立审查及两次定向复审通过。既有第三方 CMake 弃用/策略与可选依赖探测提示未在本任务整改；新增代码未报告编译警告。
- 计划自检裁决：序号沿用协议完整 uint64 范围，而非 JSON 时间戳的 2^53−1；代价是配置时需字符串边界比较。正式构建拒绝非零资源版本第四段，防止不同构建的显示身份重合；代价是此类发布流水线需调整版本策略。
- 任务 2：`d0fafcaf`。组合接口及 QtTest 23 项通过（含初始化和清理）；移除 Registered 门禁后四个拒绝用例失败，恢复后全绿。完整 Release 构建成功，CTest 84/84 通过（89.42 秒），日志保留在 `out/ui-vs/task-2-full-ctest.log`。任务审查无关键或重要问题；误纳入的本轮临时报告移出版本跟踪，验收摘要保留在本节。
- 主控在 `d0fafcaf` 上独立完整 Release 构建退出码 0、CTest 84/84 通过（88.94 秒），日志为 `out/ui-vs/release-identity-final-build.log` 和 `out/ui-vs/release-identity-final-tests.log`。旧安装探测内两个文件符号链接用例仍因创建权限限制跳过，未算作行为通过；新增组合 QtTest 23 项全部通过、无跳过。
- 主控确认本阶段未修改 `src/app`、`src/ui` 或 `packaging`，两服务仍为 std::nullopt，当前缓存正式构建开关 OFF、其他发布字段为空。未合并或推送，也未执行真实安装器/UAC或跨平台实际运行测试。
- 整阶段审查补充发现：读取测试不能对合法正式 ON 构建继续断言空，配置矩阵也不能在非 Windows x64 开发工具链上无条件要求正式构建成功。前述 84/84 证据只覆盖 Windows x64 OFF。两项合并为一轮最终测试修复，不改变正式更新支持平台或应用门禁。
- 最终修复 `a9e77043`：读取测试按真实构建输入断言，配置矩阵按目标描述选择正式成功或拒绝预期；新增 Linux/ARM64 受控目标描述路由测试，不代表实际跨平台编译运行。Windows x64 真实 ON 配置先复现失败，修复后读取与配置矩阵 2/2 通过；之后恢复 OFF 和三项空发布参数，原 BUILD_VERSION 不变。
- 最终修复者的默认 Release 构建成功；首轮全套 85/86，唯一失败为未修改的 `redirectDoesNotResetTotalDeadline()`（请求数实际 1、期望 2；脚本 10ms 分块，总时限 150ms）。原样聚焦连续三次通过，再原样全套 86/86 通过（97.00 秒）。只确认偶发时序敏感，调度根因未确定，没有修改网络逻辑或宣称修复此用例。首轮、失败详情、聚焦和全套复跑日志分别为 `out/final-fix-default-ctest.log`、`out/final-fix-fetcher-first-failure.log`、`out/final-fix-fetcher-recheck.log`、`out/final-fix-default-ctest-recheck.log`。
- 最终定向复审确认上述两项均已解决，未发现修复引入新的关键或重要问题。主控在 `a9e77043` 上再次独立完整 Release 构建成功，CTest 86/86 通过（97.62 秒）；日志为 `out/ui-vs/release-identity-accepted-build.log` 和 `out/ui-vs/release-identity-accepted-tests.log`。安装探测的两项权限跳过与既有网络时序用例偶发失败仍按上述边界记录，不宣称已解决。
- 3B.1 验收完成；本轮临时审查资料清理前已将裁决、提交和验证结论归档于本节。保留开发分支、工作树和构建日志，等待集成选择；3B.2 尚未开始，正式安装执行仍未启用。
