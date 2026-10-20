# 桌面更新实施与验收

目标：启动后后台检查，手动检查显示结果；更新窗口下载、校验、安装。覆盖现有 Windows 安装包/便携 ZIP、macOS Intel/ARM 应用和 Linux DEB/RPM。保留用户数据，不扩展为通用 SDK，不新增发行形式，不迁移旧客户端。

## 实现边界

- 保留 `src/update` 的签名协议和 `src/updateqt` 的下载缓存。
- Windows 安装版继续使用现有受限安装交接；便携版使用独立辅助程序，只更新包拥有的文件。
- macOS 使用 Sparkle 标准界面和安装流程；通过小型 Qt/Objective-C++ 适配接入菜单和启动检查。
- Linux 保留现有 Qt 更新界面，校验下载后调用系统包安装程序；明确展示取消、失败和日志。
- 发布工具从同一发布记录生成签名 JSON 与 macOS appcast，先验证发布包，再发布索引。
- 数据保存由应用完成；程序更新不删除配置、会话、日志及未知用户文件。

## 任务与文件

- [x] 1. 公共协议和发布：`src/update/`、`cmake/ZzReleaseIdentity.cmake`、`scripts/ci/sign_update_manifest.py`、`publish_update_feed.py`、`release_assets.py`、相关 `tests/update/` 和 `tests/ci/`。支持现有发行格式、精确选包、构建身份、便携包标记及 macOS appcast。验证错误格式、错误架构、签名、旧清单、缺失资产均不产生可安装结果。
- [x] 2. Windows 便携安装：新增独立的安装实现和辅助程序，使用现有归档库展开 ZIP。验证包根目录、路径穿越、链接、大小、占用、备份恢复及用户数据保留。交接后等待宿主退出，替换失败恢复，完成后重新启动。
- [x] 3. macOS：固定 Sparkle 版本与校验值，接入构建、bundle、签名、公证和 appcast 公钥。仅官方发行启用线上检查。菜单手动检查，启动后台检查；安装授权、退出、替换由 Sparkle 管理。
- [x] 4. 应用集成与 Linux：`src/updateqt/` 安装身份、缓存扩展名、Linux 安装服务；`src/app/` 更新入口；`src/ui/src/updatecheckdialog.cpp` 状态与诊断。Windows 保持原受限安装路径，便携版选择新路径；Linux 识别真实包所有者及格式后通过系统授权安装。
- [x] 5. 验证与文档：构建 `ci_build`，运行 CTest 与 Python 发布测试，核对平台打包和 CI 原生测试路径；更新用户文档并记录 Windows/macOS 本机验证限制。

## 验收行为

1. 后台检查不阻塞主界面；手动检查有确定结果。
2. 下载失败可重试，校验不通过不能安装。
3. Windows 安装版不因便携支持而放松所有者、目录或凭据校验。
4. 便携更新只操作允许的程序文件；安装失败可恢复，用户数据保留。
5. macOS 的 Sparkle 框架确实进入发行 bundle，公钥与发布签名一致。
6. Linux 不直接覆盖由包管理器管理的程序；失败保留应用和已校验下载。
7. 每项关键行为先添加回归测试并观察失败，再完成实现；不把源码字符串测试当作运行验证。
8. 本地 Linux 测试成功与 Windows/macOS 实机安装成功分别报告。

## 进度

- 初始状态：`e4e94d0`，当前工作区干净，管理员凭据修复已经本地提交，尚未发布。

- 实现完成：现有签名检查/下载接入 Windows 安装版与便携版、Linux 系统包；macOS 接入固定版本 Sparkle。启动时的 Linux 安装来源探测在后台进行，安装前重新探测；预览版首次运行默认采用预览频道。
- 独立审查修正：便携辅助程序 READY 后仍允许显式取消；仅 Sparkle 的安装退出提交已准备会话，普通退出仍保持原保护；Windows 应用测试补齐便携库链接，macOS CI 开启原生退出保护测试。
- 本地验证：`ci_build` 构建成功；离屏模式 CTest 90/90 通过；Python CI/发布测试 99/99 通过；`git diff --check` 通过。测试包含安装命令真实子进程、源包篡改、失败日志、存活事务、ZIP 非法路径、文件保留/恢复、频道默认值及更新对话框。
- 验证边界：本机 Linux；Windows 辅助程序仅完成交叉编译/链接检查，Windows Qt 原生生命周期与 macOS Sparkle 测试已纳入 CI，尚未执行。真实 UAC、pkexec、系统包安装、签名/公证与完整更新替换按 `DESKTOP_UPDATE_ACCEPTANCE.md` 继续验收。
- 本轮修改保留在当前仓库工作区，未提交、推送或更新远端标签。生产启用需发布新的版本，旧客户端迁移不在本轮范围内。
- 复测观察：一次 `-j4` 全量运行中既有 `zzlogg_ui.line_number_ui` 卡住，捕获栈显示主线程在 `LogDataWorker` 析构等待线程池、后台索引线程等待读写锁。终止该测试后其余 89 项通过，单独复测该项通过；此日志加载/销毁偶发现象尚未修复，本轮未修改相关日志引擎。证据保存在 `/tmp/zzlogg-desktop-ctest-final.log` 与 `/tmp/zzlogg-line-number-recheck.log`。
- 最终确认：增加 60 秒单测试超时并以 `-j2` 重新全量执行，90/90 通过（20.31 秒）；上面的偶发等待记录仍保留。最终日志 `/tmp/zzlogg-desktop-ctest-confirmed.log`。
