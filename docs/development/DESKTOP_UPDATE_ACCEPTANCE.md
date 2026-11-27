# 桌面更新验收与恢复

适用于 Windows 注册安装与便携 ZIP、macOS Intel/Apple Silicon、Linux DEB/RPM 的更新路径。每项记录操作系统、架构、旧/新包的发布序号、构建提交、结果及日志；未实际执行的项目标记为待验证，不用静态检查或其他平台测试代替。

本轮实现是在 Linux 环境完成。Windows/macOS 原生编译、真实授权和安装替换仍待相应 CI 与原生环境；Linux 的无特权子进程夹具也不替代真实系统包安装。

## 通用检查

1. 关闭生产源开关或使用开发构建，确认不能误启动生产自动安装。
2. 使用隔离的测试密钥和测试发布记录，验证手动检查有结果、后台检查不阻塞窗口。续签同一发行不产生新版本提示；更高预览构建序号可更新相同显示版本。
3. 分别注入签名错误、哈希错误、缺失文件、错误架构、错误安装形式和过期 JSON，确认没有启动安装器。
4. 打开多个窗口和日志文件，记录会话与配置；更新成功并重启后恢复。注入会话保存失败，确认留在原应用中且可继续操作。
5. 下载失败可以重试，安装失败展示具体原因与日志入口，原始已下载缓存保留。记录用户数据目录并检查未知用户文件没有被删除。

## Windows 注册安装

继续执行 [UPDATE_ACCEPTANCE_VM.md](UPDATE_ACCEPTANCE_VM.md) 中的 UAC 接受/拒绝、安装身份、并发实例、文件占用和授权恢复检查。此路径保留现有受限入口，便携支持不允许放宽注册安装的目录、身份或凭据校验。

## Windows 便携 ZIP

在本地固定磁盘建立可写的完整便携目录，必须含 `ZzLogg.exe`、`ZzLoggPortableUpdate.exe`、`.zzlogg-portable` 和 `.zzlogg-portable-files.json`。在目录中另外放入用户日志与配置文件，用于确认保留行为。

- 成功路径：校验包、保存会话、辅助程序就绪、应用退出、只替换清单文件、重启；核对新增和被新版删除的程序文件。
- 拒绝路径：网络盘、可移动磁盘、只读目录、链接或路径穿越、错误包根目录、未知用户文件与新包路径冲突、另一个实例仍占用目录。
- 失败路径：锁定待替换文件、磁盘空间不足、辅助程序未就绪；检查应用是否保持可用，已修改文件是否回滚，并保留故障日志。

更新暂存位置为 Qt `CacheLocation` 下的 `portable-updates/<事务 UUID>/`，实际位置由失败提示或 `portable-update.log` 给出。辅助程序保留 `backup/`、`payload/` 和 `plan.txt` 用于诊断与恢复。

**断电或强制终止后的恢复为人工操作，不是自动恢复承诺：**

1. 关闭该目录的所有 ZzLogg 实例及更新辅助程序。先复制当前程序目录和整个暂存事务目录作为恢复前备份，不要直接再次运行更新。
2. 查看 `portable-update.log` 和 `plan.txt`。确认 `backup/` 是本次替换前的旧程序文件；日志不足以证明备份完整时，优先在新目录解压一份可信的完整便携发行包。
3. 将 `backup/` 内文件按原相对路径恢复到程序目录，包括旧文件清单。仅删除由该事务新增且旧清单未拥有的程序文件、以及确认属于本次操作的 `.zzlogg-update-new` 临时文件；不要清空程序目录，也不要删除未知用户文件。
4. 手动启动并核对版本、会话、配置和用户日志。确认恢复后再清理暂存备份；恢复操作仍失败时保留全部证据，使用新目录中的完整发行包启动。

## macOS

分别覆盖 Intel macOS 13.3 及以上、Apple Silicon macOS 14.0 及以上：

- 在官方且启用源的 bundle 中确认 `CFBundleVersion` 为发布序号、appcast 地址对应架构/渠道、公钥一致，`Sparkle.framework` 及其辅助程序完整。
- 在原生 CI 编译并运行 `zzlogg_update.mac_exit`，验证保存成功、保存失败、安装失败及重试时的退出保护。此测试不启动真实安装器。
- 在真实 bundle 中验证 Sparkle 后台检查、手动检查、标准下载界面、取消、授权、替换与重启；篡改 DMG 应拒绝安装。
- 使用签名产物执行 `codesign --verify --deep --strict`，确认公证获批且 DMG 已装订；从打包后的 DMG 安装并运行，不能只测试构建目录中的应用。
- 在会话保存失败时确认不退出；成功替换后检查用户配置与日志仍存在。

## Linux

分别在 DEB 和 RPM 环境安装旧包，确认当前可执行文件确由已安装的 ZzLogg 包拥有，且架构为 x64。

- 非 root 用户通过 `/usr/bin/pkexec` 请求系统授权；root 用户直接运行包管理器。测试授权接受、拒绝、无授权代理和缺失工具。
- DEB 使用 `apt-get --reinstall --yes install`，RPM 使用 `rpm --upgrade --replacepkgs --`。覆盖新显示版本和同显示版本的新预览构建，核对安装后的程序版本/发布序号，避免只检查命令退出码。
- 制造依赖冲突或包数据库占用，确认非零退出显示失败并恢复界面。RPM 路径不自动下载缺失依赖。
- 安装期间关闭窗口或请求退出应被阻止；成功后保存会话的应用正常退出并重启，失败后保留原下载缓存。
- 日志位于 Qt `AppLocalDataLocation` 下的 `updates/install-logs/<UUID>.log`，界面可直接打开。应包含包管理器输出、退出状态和错误原因。

Linux 复制与校验使用私有的 `0700` 暂存目录和 `0600` 包文件。销毁界面对象不会主动终止已经运行的系统包事务；强制杀进程或断电仍应使用发行版的包管理恢复流程，不能据此宣称自动回滚。

## Windows：提交前启动检查与真实重启验收

更新器现在分两次确认：文件替换后、事务提交前运行 `ZzLogg.exe --update-probe`，检查 Qt 原生平台插件、隔离临时存储及基础控件初始化；探测失败则恢复旧文件（安装版同时恢复注册表版本）。正式重启还必须等候真实用户存储和主界面初始化完成、事件循环发出 StartupReady，不能仅凭进程创建成功或单实例端口出现判定成功。

探测进程若无法终止，保留事务/备份供恢复，不在它仍占用程序文件时回滚。便携版的回滚目前仍是进程内回滚，不具备断电后自动重放的持久化事务日志；不要把本次改造当作断电恢复保证。安装版原有 journal/recover 路径继续适用。

### 两个本地版本（在隔离 Windows 测试机中）

使用 x64 MSVC 开发终端构建两个带本地测试 feed 的版本。以下版本号仅用于测试：

```powershell
python scripts/dev/windows_local_update.py build --version 26.11.90 --qt D:\SoftWare\Qt\6.11.1\msvc2022_64
python scripts/dev/windows_local_update.py build --version 26.11.91 --qt D:\SoftWare\Qt\6.11.1\msvc2022_64
```

安装 `out/local-windows/packages/26.11.90/` 中的旧包，关闭程序，然后从**非管理员终端**执行：

```powershell
python scripts/dev/windows_local_update.py serve --version 26.11.91 --app 'C:\Program Files\ZzLogg\ZzLogg.exe' --verify
```

在自动打开的程序中正常点击检查更新、下载、安装，并确认 UAC。验收器自动检查旧 PID 退出、新 PID 有可响应的主窗口且继续存活、二进制哈希改变、目标版本一致、测试数据保留、更新器结果为 complete。900 秒内未完成或检测到失败，命令返回非零；结果写入 `out/local-windows/acceptance-26.11.91.json`。这仍需用户点击更新/UAC，不是假装全自动的模拟升级。

套件用例 08 也可传入 `-UpdateOriginalProcessId`、`-UpdateExpectedVersion`、`-UpdateDataDirectory` 观察已启动的旧版程序，不再固定跳过。缺少现场参数仍明确 SKIP，不计作通过。

### 诊断与兼容

- 安装版协调日志：当前 `--data-dir` 对应数据根目录下的 `logs/update-last.log`，记录失败阶段、子进程退出码及最后结果，不包含认证 token。
- 安装版引擎日志/备份：`%ProgramData%\ZzLogg\UpdateTransactions`。
- 便携版日志/备份：此次更新的 staging 目录内 `portable-update.log` 和 `backup`。
- 从未包含这次改造的旧版本启动更新时，仍由旧版本携带的协调/便携 helper 运行；目标安装包中的新引擎可以执行新探测，但完整的新重启确认和协调日志需要测试基线也含本次改造。因此必须做上述两个本地版本测试，并另测正式旧版本迁入路径。
- Windows 原生 CTest 新增 `zzlogg_updater.startupcheck`（失败退出、无确认、超时、过早 IPC）、`zzlogg_updater.application_probe`（实际 Qt GUI exe 经原生探测器启动）及 `zzlogg_updater.application_readiness`（实际主实例启动就绪）。Linux 的 offscreen 结果和 MinGW 交叉编译不能替代这些 Windows 检查。
