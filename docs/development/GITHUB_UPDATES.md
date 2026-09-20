# GitHub Releases 更新接入

当前接入范围是**验签后检测版本 + 打开 GitHub 手动下载**。支持稳定版和预览版两个渠道，适用于 Windows、macOS 和 Linux 客户端。签名清单暂只描述 Windows x64 安装包；其他平台通过发布页选择对应安装包，不在程序内下载或执行该 Windows 包。

目前没有 Windows 代码签名证书。Ed25519 清单签名只能证明更新信息来源和内容，不能替代 Windows Authenticode 签名。主程序仍未提供安装身份和生产安装执行器，程序内的“下载更新”“退出并安装”保持关闭；本次没有放宽安装包校验。

## 客户端行为

- 更新源未启用时，明确显示未配置，并提供“前往 GitHub 下载”。
- 清单验签、有效期、渠道、防重放和本地状态持久化全部通过后，比较 `YY.MM.PP` 显示版本。稳定版发现更高版本时提示手动下载，否则显示已是最新版。
- 预览版可能多个构建共用一个显示版本。显示版本未增加时，仅在手动检查中展示已验证的版本信息，不声称已安装最新每日构建，也不因每天续签重复弹窗。需要通过发布页选择最新预览包。
- 手动下载按钮只打开固定项目地址：稳定版 `/releases/latest`，预览版 `/releases/tag/continuous-build`；不会执行来自清单的任意网址或安装命令。

## 公钥与构建开关

版本管理中的公钥：`packaging/update/github-public-key.json`。新密钥 ID 为 `zzlogg-update-2026-09`，客户端和发布器共用此记录。私钥必须保存在仓库外，不能加入 Git、构建产物、日志或文档。

本地构建默认不连接生产更新源；使用 `-DZZLOGG_ENABLE_GITHUB_FEED=ON` 可编译内置 GitHub 源和生产公钥。既有显式 `ZZLOGG_UPDATE_*` 参数优先，关闭开关不会把默认值残留在 CMake 缓存中。CI 通过仓库变量 `ZZLOGG_UPDATE_FEED_ENABLED` 控制此选项，新构建目录才会从环境变量初始化该 CMake 选项；复用本地目录时应显式传 `-D`。

更新源：

```text
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/stable.json
https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/preview.json
```

## 发布与续签

`Signed Update Feed` 工作流独立于 Publish（正式版与预览版发布），支持手动触发、成功发布后触发、每天北京时间 01:30 续签。GitHub 定时任务可能延迟，长期无活动也可能被停用；清单有效期为 14 天，超过有效期客户端拒绝使用，应监控工作流失败并及时恢复续签。

工作流只执行 `master` 代码，不执行触发工作流的分支或产物内代码；私钥只注入最终发布步骤。发布器读取当前已公开的稳定版和预览版，验证 `release-info`、附件地址、长度和 SHA-256，再生成并验证签名封装。没有预览版时跳过该渠道。缺包、草稿、格式不符、密钥不匹配或校验失败均不发布该渠道。

独立 `update-feed` 分支承载两个 JSON 文件。首次从 `master` 创建分支以保留现有树，之后只更新对应渠道文件，不移动发行标签。更新用旧文件 SHA 防止覆盖并发修改，拒绝版本或发布序号倒退；稳定版同版本不能悄悄替换发行包或安装策略，允许更正发布说明并重新签名。两个渠道分别提交，一个渠道失败仍会尝试另一个渠道续签，最后将本次工作流标记为失败以便排查。

`metadataSequence` 使用 UTC 毫秒时间且必须比既有值增加；`releaseSequence` 是去掉点号的显示版本整数（如 `26.09.01` 为 `260901`），续签保持不变。预览包同版本可更换，但当前客户端不据此判断每日构建升级。若以后要接通自动安装，需先确定正式安装身份、数据 schema、发布序号迁移和每日构建升级政策。

## 首次上线顺序

以下是常规首次上线流程；本次用户明确要求沿用 v26.09.01 重新发布，因此先备份旧产物，再由维护者更新该标签及发行包。发布器仍拒绝直接覆盖公开稳定版，重发期间需先将该发行版恢复为草稿并更新目标提交。已有旧 v26.09.01 用户需要手动下载重装，同版本号不会触发升级提示。

1. 审阅并提交本次改动到 `master`，确认 CI 通过。已有 v26.09.00/v26.09.01 二进制不含新配置，不能被远端改造成支持新更新源的客户端。
2. 备份仓库外新私钥。将 PKCS8 PEM 原文上传到仓库 Actions Secret **`ZZLOGG_UPDATE_SIGNING_KEY`**。可用 `gh secret set ZZLOGG_UPDATE_SIGNING_KEY --repo jackfahdin/ZzLogg < /受保护目录/密钥.pem`，避免把内容放入命令参数或打印出来。
3. 设置仓库 Actions Variable **`ZZLOGG_UPDATE_FEED_ENABLED=true`**，并手动运行 **Signed Update Feed**。确认成功创建两个源（没有预览版则仅稳定源）、公钥匹配且客户端能访问。若 Actions 分支规则阻止写 `update-feed`，应为此分支设置合适的规则。
4. 清单上线后，递增项目版本，推送新的版本标签，让新版发行包编入更新源。常规发布不要重打已有正式版本标签；本次 v26.09.01 重发为用户明确授权的例外。
5. 用户首次手动安装含新配置的版本；以后可在应用中检测更高版本，再通过 GitHub 下载安装。

Windows 一键安装是后续独立工作：代码签名与发布者校验、可信安装身份、生产安装协调器，以及 Windows 实机的提权、替换、回滚和重启验证缺一不可。

## 本地验证

```sh
python3 -m pip install -r scripts/ci/requirements-update.txt
python3 -m unittest discover -s tests/ci -p 'test_*.py'
cmake --build /你的构建目录 --target ci_build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /你的构建目录 --output-on-failure -j4
```

签名与发布测试使用临时测试密钥及模拟 GitHub，不上传生产私钥、不写远端。CI 已在运行这些 Python 测试前安装固定版本的签名依赖。

本次 Linux / Qt 6.11.2 本地验收：`ci_build` 成功，82 项 CTest、59 项 Python CI 测试通过；另以临时测试密钥运行实际 Python 签名器，产物交由生产 C++ `verifyManifest()` 验签通过。生产私钥与仓库公钥匹配、仓库外目录 0700/文件 0600 权限已检查。未执行线上工作流或 Windows 自动安装实机测试。

## 精简后的工作流

| 名称 | 触发与职责 |
| --- | --- |
| CI Build | PR、手动，以及供 Publish 调用的全平台构建和测试；普通 master push 不再重复编译 |
| Publish | tag 正式发布；北京时间 00:00 有新提交才发布预览版；手动运行发布 master 预览版 |
| Signed Update Feed | Publish 成功后签名，另每天续签与手动运行；不编译应用 |
| CodeQL | 每周与手动安全扫描 |
| Update Smoke | 安装器相关文件变化或手动运行，保留专项 Windows 安装验收 |

旧 Test env 调试工作流已删除。更新清单工作流监听统一的 Publish。PR 新提交会取消同一 PR 过时的构建，发布流程不会被自动取消。普通 master 提交需等定时发布、打 tag 或手动 CI 才会获得全平台构建结果。

## 更新日志维护

`CHANGELOG.md` 是版本变化的统一来源，从 26.09.01 重新开始。使用 `## [26.09.01] - 2026-09-20` 格式；下次正式发布前添加新版本条目，开发中的变化放在 `## [Unreleased]` 下。正式发布只读取对应版本，预览发布合并 Unreleased 与当前版本；prepare 阶段会拒绝缺少说明的发布。发布器从构建所用的同一个提交读取文件，清单签名器再把 GitHub Release 正文转换成纯文本供应用显示。

开启生产更新源后，菜单集成测试使用仅链接进测试程序的离线配置 fixture，避免依赖 GitHub 网络。应用和生产配置测试仍链接真实配置；已在 `ZZLOGG_ENABLE_GITHUB_FEED=ON` 下完成完整构建和 82 项 CTest。
