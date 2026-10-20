# GitHub Releases 桌面更新

官方构建支持启动后后台检查和菜单手动检查。用户确认后，在更新界面下载、校验并安装；程序在退出前保存会话。稳定版和预览版分别发布，现有安装形式保持不变。

## 各平台安装路径

| 安装形式 | 更新路径 | 条件与限制 |
| --- | --- | --- |
| Windows x64 注册安装版 | 签名清单 → 下载与 SHA-256 校验 → 现有受限安装交接与 UAC → 重启 | 必须匹配 HKLM64 安装登记及当前目录；保留原有权限、凭据和事务恢复检查 |
| Windows x64 便携 ZIP | 签名清单 → 校验和安全解压 → 独立辅助程序等待应用退出 → 按文件清单替换 → 重启 | 完整便携包必须含标记、文件清单和辅助程序；目录必须位于可写的本地固定磁盘，不支持网络盘或可移动磁盘 |
| macOS Intel / Apple Silicon | Sparkle 标准界面 → 下载并验证 DMG 的 Ed25519 签名 → 系统授权、应用替换与重启 | 官方构建且已启用更新源；按构建架构和渠道使用独立 appcast；最低系统分别为 macOS 13.3 / 14.0 |
| Linux x64 DEB / RPM | 签名清单 → 校验并复制到私有目录 → `apt-get` 或 `rpm` → 保存会话后的重启 | 当前可执行文件须由已安装的 ZzLogg 系统包拥有；非 root 用户通过 `pkexec` 授权；不直接覆盖包管理器拥有的文件 |

Windows 清单中的 `format: nsis-exe` 是兼容既有协议的历史枚举；实际安装包使用 Inno Setup。当前没有 Windows 代码签名证书，信任根是 Ed25519 签名清单所绑定的长度和 SHA-256，UAC 仍可能显示“未知发布者”。

便携更新只替换包文件清单拥有的文件，保留配置、会话、日志及未被清单管理的用户文件。正常文件操作失败时尝试回滚，并保留日志和备份；**断电或强制终止不具备自动事务恢复保证**，需要人工从备份恢复，见[桌面更新验收与恢复](DESKTOP_UPDATE_ACCEPTANCE.md)。

Linux 使用固定路径的 `/usr/bin/apt-get --reinstall --yes install <包路径>` 或 `/usr/bin/rpm --upgrade --replacepkgs -- <包路径>`，确保同版本预览包也实际替换。非 root 用户须有 `/usr/bin/pkexec` 及可用的系统授权代理；缺少工具、拒绝授权、依赖冲突或包管理器非零退出都显示失败和安装日志入口。RPM 路径不会自动下载缺失依赖，需要先用系统包管理器解决。安装开始后不可取消或正常退出应用；失败会恢复界面并保留原下载缓存。

## 校验、版本和会话

Windows/Linux 的清单必须通过签名、有效期、渠道、防重放及状态持久化检查；实际安装前重新验证选择，再检查包大小和 SHA-256。Linux 校验与复制使用同一份读取字节，安装器只取得私有目录中的副本，不会重新打开可变化的下载路径。便携 ZIP 还检查包根目录、路径穿越、链接、文件数量、大小及文件归属冲突。

稳定版 `releaseSequence` 是去掉点号的显示版本整数，如 `26.10.05` 对应 `261005`。预览版使用构建工作流的 `GITHUB_RUN_ID`；即使 `YY.MM.PP` 显示版本相同，更新的构建序号仍可升级。同一次工作流重跑保持序号不变。每日续签只递增 `metadataSequence`，不会把同一发行包伪装成新版本。

macOS 使用 Sparkle 的签名与版本规则，`CFBundleVersion` 和 appcast `sparkle:version` 都使用发布序号，`CFBundleShortVersionString` 保留显示版本。appcast 固定到官方构建所选的架构和渠道。JSON 清单的 14 天有效期与防重放状态不用于 Sparkle appcast。

安装前必须完成会话保存；保存失败则拒绝退出。macOS 通过 Sparkle 的安装前回调检查保存结果，Qt 原生退出事件提交已经准备好的会话。Linux 在包管理器执行期间保持应用和会话准备状态；安装失败恢复操作，成功后正常关闭并重启。旧客户端不会通过远端清单获得新更新能力，仍需首次手动安装支持本流程的版本。

## 公钥与构建开关

公钥记录位于 `packaging/update/github-public-key.json`，密钥 ID 为 `zzlogg-update-2026-09`。Sparkle `SUPublicEDKey` 使用同一 Ed25519 公钥的 Base64 编码。私钥保存在仓库外或 Actions Secret `ZZLOGG_UPDATE_SIGNING_KEY`，不得进入 Git、产物、日志或文档。

- `ZZLOGG_OFFICIAL_RELEASE=ON`：构建带正式发布身份的客户端；普通本地构建和 PR 默认关闭。
- `ZZLOGG_RELEASE_CHANNEL=stable|preview`：官方构建渠道。预览构建还传入 `ZZLOGG_RELEASE_SEQUENCE`。
- `ZZLOGG_ENABLE_GITHUB_FEED=ON`：启用内置生产更新源。CI 从仓库变量 `ZZLOGG_UPDATE_FEED_ENABLED=true` 初始化；复用构建目录时显式传 `-D`，不要依赖旧缓存。
- macOS 必须同时满足官方身份和更新源开关，才链接、嵌入和启动 Sparkle。关闭更新源仍保留官方发布序号，避免构建版本排序改变。
- Windows/Linux 的显式 `ZZLOGG_UPDATE_*` 源配置仍优先于 GitHub 默认值。本地测试源使用 `ZZLOGG_UPDATE_LOCAL_FEED=ON`，不得用于分发构建。

源地址均位于：

```text
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/stable.json
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/preview.json
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-x64-stable.xml
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-arm64-stable.xml
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-x64-preview.xml
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-arm64-preview.xml
```

未配置更新源或不具备安装身份时，保留明确提示和固定 GitHub 发布页入口；不会执行来自任意网址的安装命令。稳定版下载页为 `/releases/latest`，预览版为 `/releases/tag/continuous-build`。

## 发布、签名与续签

`Publish` 组装同一工作流的六个安装产物：Windows 安装包与便携 ZIP、Linux DEB 与 RPM、macOS 两种架构的 DMG，并写入包含发布序号、长度和 SHA-256 的 `release-info`。便携 ZIP 同时带有 `.zzlogg-portable` 和 `.zzlogg-portable-files.json`。

`Signed Update Feed` 从 `master` 执行可信发布代码，在发布成功后、手动触发或每天北京时间 01:30 运行。发布器先校验完整发布记录和全部附件，再签名 JSON 与两个 macOS appcast；DMG 签名直接覆盖最终 DMG 字节。缺包、草稿、架构/渠道不符、密钥不匹配或任何哈希错误均阻止该渠道发布。JSON 清单有效期为 14 天；定时任务可能延迟或停用，应处理工作流失败，避免清单过期。

每个渠道的 JSON 与两个 appcast 在 `update-feed` 分支的一次提交中原子更新。该分支使用独立历史，不包含源码；发布使用开始时取得的分支提交作为基线，只允许非强制快进，竞争发布会失败，不能覆盖较新的结果。发布拒绝版本或发布序号倒退；稳定版同版本内容不可变，允许更正说明和续签。两个渠道分别处理，一方失败不阻止尝试另一方，最终工作流仍报告失败。

Sparkle 固定为 2.10.0，并验证固定 SHA-256。框架及辅助程序进入应用 bundle，打包时从内部程序到框架、应用逐层签名并保留所需权限。签名身份必须匹配配置的开发团队；公证返回 `Accepted` 后才装订 DMG。CPack 原样复制已部署、已签名的 bundle，避免修改 RPATH 破坏签名。

生产开通需要同时准备签名密钥、更新源仓库变量和对应平台发布凭据，并完成原生验收。首次启用应发布新的稳定版本，不重打已有正式标签；旧包需要用户手动安装一次。`CHANGELOG.md` 是发行说明来源，预览版合并 `Unreleased` 与当前版本条目。

## 验证与证据边界

```sh
python3 -m pip install -r scripts/ci/requirements-update.txt
python3 -m unittest discover -s tests/ci -p 'test_*.py'
cmake --build /你的构建目录 --target ci_build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 --output-on-failure -j4
```

Python 测试使用临时密钥和模拟 GitHub，不发布远端内容。Linux 安装器测试执行无特权的真实子进程夹具，覆盖私有副本、篡改拒绝、失败日志和生命周期；这不等于完成了真实 `pkexec` 或系统包事务验收。

本轮工作环境为 Linux。Windows 原生辅助程序、UAC 和完整安装替换，以及 macOS Objective-C++、Sparkle 界面、签名、公证和实际替换，仍需对应平台 CI 与原生环境验证。Linux 本地测试成功不能作为 Windows/macOS 实机通过的证据。执行清单和人工恢复步骤见[桌面更新验收与恢复](DESKTOP_UPDATE_ACCEPTANCE.md)；现有 Windows 注册安装专项见 [VM 验收手册](UPDATE_ACCEPTANCE_VM.md)。
