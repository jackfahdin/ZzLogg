# Windows 本地编译与更新测试

这套入口用于在 Windows 上修改后增量编译、生成测试安装包、验证“检查 → 下载 → 安装 → 重启”，无需推送 GitHub 或等待 CI。

## 首次准备

在 **x64 MSVC 开发者终端**进入仓库根目录。MSVC 环境由你设置；终端内需要能执行 `cl`、`cmake`、`ninja`、`git`、`python`。Python 要求 3.11 或更新版本，CMake 建议 3.25 或更新版本。

Qt 默认位置为 `D:\SoftWare\Qt\6.12.0`。脚本会在其中寻找完整的 `msvc*_64` kit，也可以直接传 `--qt D:\SoftWare\Qt\6.12.0\msvc2022_64`。Qt 需要包含 Core5Compat、Svg、LinguistTools 等项目依赖，详见 [BUILD.md](BUILD.md)。不能使用 MinGW 或 ARM kit。

首次执行：

```powershell
git submodule update --init --recursive
python -m pip install -r scripts/ci/requirements-update.txt
```

依赖和 Inno Setup 编译器首次下载需要联网。Inno Setup 复用现有固定版本及 SHA-256 校验，首次安装编译器时可能需要系统授权；之后使用本机缓存。

## 1. 构建并安装基准版本 A

```powershell
python scripts/dev/windows_local_update.py build --version 26.10.05
```

安装包输出到：

```text
out\local-windows\packages\26.10.05\ZzLogg-26.10.05-x64-Qt6-setup.exe
```

手动运行这个安装包。完成页不要勾选启动软件；如果已经启动，先退出。后面需要由测试脚本启动，才会携带本地更新源和独立测试数据目录。

这是含当前修改的 **本地 26.10.05 测试构建**，并非过去 GitHub 上的同名版本。安装包仍使用 ZzLogg 的同一个注册项，会更新现有安装登记；建议在 Windows 测试虚拟机或可以替换的安装上操作，不要把它当作与正式安装并存的独立产品。记录实际安装路径，例如 `D:\Software\ZzLogg\ZzLogg.exe`。

## 2. 增量构建新版 B

```powershell
python scripts/dev/windows_local_update.py build --version 26.10.06
```

第二次复用 `out\local-windows\build` 和依赖缓存，默认关闭 LTO、Hyperscan、Vectorscan 和测试目标，只构建程序运行目录及安装所需辅助程序。需要降低内存占用时加 `--jobs 4`。

`--version` 同时控制程序显示版本、发布身份和安装包版本；通过 CMake 的 `BUILD_VERSION` 参数实现，不修改源码里的正式版本号。两个安装包分别保留在各自版本目录中。新版 B **不要手动安装**，下一步让 A 下载它。

## 3. 启动本地更新源并测试

先退出所有正在运行的 ZzLogg 实例，尤其是刚才安装器自动打开的实例。使用**非管理员终端**运行下面的命令；这一步只需 Python，不要求 MSVC 环境，可以新开普通 PowerShell 并进入仓库根目录。以下 `--app` 替换为 A 的实际安装路径：

```powershell
python scripts/dev/windows_local_update.py serve --version 26.10.06 --app "D:\Software\ZzLogg\ZzLogg.exe"
```

脚本会检查 B 安装包与构建记录中的哈希一致，用独立 Test 密钥生成签名清单，在 `127.0.0.1:8765` 提供更新源，然后启动 A。无需复制地址、公钥或手动设置环境变量。

在软件中点击“检查更新”，应发现 `26.10.06`；点击下载、安装，按提示处理 UAC，更新后在关于窗口核对版本。**更新完成前保持终端运行**；结束测试后 `Ctrl+C` 停止服务器。端口占用时添加 `--port 8766`。服务器端口绑定失败时不会启动客户端。

如果用管理员终端运行 `serve`，软件会继承管理员权限，现有安装协调流程可能返回“需要手动重启”；这不等同于安装失败，但无法据此验证普通用户的自动重启路径。脚本会提示当前终端是否处于此情况。

测试配置与会话目录为 `out\local-windows\user-data`。本地更新信任状态及下载缓存使用 Test 命名空间。通过脚本启动会注入测试环境；普通双击启动不会自动连接本地源。下次验证更新时仍应先退出软件，再用 `serve` 启动。

## 日常重复验证

已更新至 B 后，改完代码构建 `26.10.07`，然后用 `serve --version 26.10.07 --app ...` 验证 B → C。不用每次重新安装 A。

如果修复的是**运行中的旧客户端**在检查、下载或安装交接阶段的问题，先手动安装包含修复的基准包，再测试到更高版本；只修改待下载的新包不能修复旧客户端正在执行的代码。

不要删除 `out\local-windows\build` 或 `out\cpm-cache`，否则需要重新编译或下载依赖。打包使用临时工作目录复制现有 Inno 脚本，不覆盖仓库根目录的 `release`、`txpayload`。

这些包启用了 `ZZLOGG_UPDATE_LOCAL_FEED`、关闭了生产 GitHub 源，只供本地测试，不用于正式发布。本入口目前串联 **Windows 安装版**；便携 ZIP 的测试按 [DESKTOP_UPDATE_ACCEPTANCE.md](DESKTOP_UPDATE_ACCEPTANCE.md) 执行。

## 排错与验证范围

- 找不到 Qt：指定完整 `--qt` kit 路径；若包含多个 MSVC kit，脚本不会自行猜选。
- 找不到 `cl`：使用 x64 MSVC 开发者终端，普通 PowerShell 默认没有编译器环境。
- 没有发现更新：确认是脚本生成并安装的 A、B 版本更高，且启动前所有旧实例已退出；不能直接从构建输出目录运行 A 进行安装版升级测试。
- 下载或安装失败：保留软件提示、安装日志，以及终端输出。脚本遇到编译、清单或打包失败会停止，不会把失败的构建标记为新包。

开发环境已验证 Python 目录识别、隔离打包、安装包篡改拒绝、真实本地 HTTP 下载、签名校验和客户端环境传递。MSVC 编译、PowerShell/Inno 打包及 UAC 安装仍需在 Windows 机器实际运行。
