# P10：Windows TLS 依赖审计与原生验证入口

**基线：** `4aba61f`。遵循已确认路线：减少依赖必须有行为证据，本轮在 Linux 完成静态审计和可复现工具，不把 Linux 结果当作 Schannel 验证。

**方案：** 保留当前 Windows 打包依赖，增加显式 Qt Network 诊断工具。它记录可用／选中 TLS 后端、Qt／SSL 版本、插件路径，以及 Windows 实际加载的模块；发起严格校验证书的 HTTPS 请求，校验内容 SHA-256，或明确要求证书握手失败。提供运行目录副本中的无 OpenSSL 对照步骤。直接删除 DLL 证据不足；直接强制应用后端会改变运行行为；本轮采用不接入应用的诊断方案。

## 文件与步骤

- [x] `tools/diagnostics/tls/tlsprobe.cpp`、`CMakeLists.txt`：独立 `zzlogg_tls_probe`，在 `KLOGG_BUILD_UI_TESTS` 下显式构建，不进入应用或 `ci_build`。支持后端选择、HTTPS 请求、内容哈希、严格证书失败验证、请求级 CA、代理及超时；JSON 记录结果，失败非零退出。
- [x] `tests/tools/test_tls_probe.py`：本地临时 TLS 服务与 CONNECT 代理，生成仅测试使用的短期证书，验证成功、错误哈希、信任失败、主机名错误、HTTPS 重定向、降级拒绝、代理及不可用后端。用缺少目标文件确认初始失败，再构建运行实际工具。
- [x] `tools/diagnostics/tls/README.md`：Windows 从实际安装／便携目录复制隔离实验目录，记录 DLL 清单；分别保留与移除副本中的 OpenSSL，限制插件和 DLL 搜索路径，对照默认后端与 Schannel，并保留 JSON 与错误日志。
- [x] 完成 Linux 本地真实 TLS 测试、现有构建／CTest／Python CI、独立审查，更新路线图，中文提交，不推送。

## 验收边界

工具只验证 Qt Network 的传输能力，不能代替实际 ManifestFetcher、PackageDownloader、Downloader 的完整应用行为测试。自定义 CA 仅作用于诊断请求，不安装系统根证书、不忽略 TLS 错误。原生验收必须包含系统证书库、更新清单、安装包下载、远程日志、重定向、实际代理配置及最终打包产物。

## 静态审计

- `.github/workflows/ci-build.yml` 与 `update-smoke.yml` 使用 OpenSSL 1.1.1 下载通道，缓存键为 `OpensslCache-1-1-1w`，设置 `SSL_DIR`。目前没有校验下载摘要或 DLL 版本，不能仅凭缓存键确认实际产物版本；原生验收需记录文件版本与 SHA-256。
- `.github/actions/agent-package-win/action.yml` 在安装包 `release/` 目录额外加入 `libcrypto-1_1-x64.dll` 和 `libssl-1_1-x64.dll`；便携包从统一 runtime 目录复制，未执行这两条额外复制。
- `cmake/DeployPortable.cmake` 调用 windeployqt；`leanruntimecontracttest.cmake` 要求 `tls/qschannelbackend.dll`。这只能证明部署规则要求 Schannel，不能证明某个产物的实际后端和 DLL 依赖。
- 更新清单／安装包通过 Qt Network 下载，遇到 `sslErrors` 失败，不绕过证书检查。远程日志 Downloader 默认校验证书，用户配置 `net.verifySslPeers=false` 才关闭校验；本轮不改变该配置，验收必须保持开启。
- 应用启用系统代理配置。诊断覆盖显式 HTTP CONNECT 的基础能力，企业代理认证／PAC／系统代理仍需 Windows 验收。
- `VerifyExternalBrand.cmake` 及 Windows 安装契约测试固定要求复制旧 DLL；未来清理必须同步调整这些契约，不能仅删工作流下载步骤。

## 当前决定

保留 OpenSSL 打包现状，P10 原生准入仍未完成。不能根据 DLL 名称或 Qt 版本推断它必然被使用／必然多余。取得实际后端、加载模块和三条应用下载路径的 Windows 证据后，再决定删除旧 DLL，或使用与 Qt 后端匹配的受维护版本。


## 本机验证结果

环境：Linux x86_64、Qt 6.11.1、GCC 15.2.0，复用 Release 构建。`--inspect` 显示可用后端 `cert-only`／`openssl`，实际选择 `openssl`，构建版本 OpenSSL 3.5.1，运行版本 OpenSSL 3.5.5。这是 Linux 能力记录，不是 Windows 后端证据，也不是性能对比。

| 验证 | 命令 | 结果 |
| --- | --- | --- |
| 显式工具构建 | `cmake --build out/build/p6-boolean --target zzlogg_tls_probe --parallel 12` | 成功 |
| 本地真实 TLS 与 CONNECT | `python3 tests/tools/test_tls_probe.py` | 14 项通过 |
| 项目构建 | `cmake --build out/build/p6-boolean --target ci_build --parallel 12` | 成功 |
| CTest | `QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/p6-boolean --output-on-failure --parallel 8` | 97/97 通过 |
| Python CI | `python3 -m unittest discover -s tests/ci -p 'test_*.py'` | 124 项，3 项跳过，无失败 |
| 默认构建边界 | `ninja -C out/build/p6-boolean -t query ci_build` | 不包含 TLS 诊断工具 |

本地 TLS 测试不读取更新清单签名、不安装软件、不写系统证书库。诊断负例要求明确证书错误和握手失败，不能把断网或超时计为证书验证成功。

独立审查未发现阻塞项，并独立复跑 14 项工具测试通过；已按审查意见区分下载通道、缓存键与实际 DLL 版本证据。Windows 分支仅静态审阅，未执行原生编译或运行。

## 尚未满足的原生准入

- [ ] 使用与 Windows 产物相同的 Qt kit／架构／配置构建并运行工具；当前无 Windows 环境，未验证 Windows 编译及 PowerShell 操作步骤。
- [ ] 安装目录与便携目录分别完成原始／无 OpenSSL 副本、默认／指定 Schannel 的四组对照；核对加载模块，排除开发机 DLL 干扰。
- [ ] 在实际应用中验证更新清单、安装包和远程日志下载，保持证书验证开启，覆盖重定向与证书错误。
- [ ] 使用真实系统信任库、系统代理、企业代理认证／PAC 验证；本地 CONNECT 不代替这些场景。
- [ ] 证实可删后同步调整工作流、安装打包及硬编码 DLL 契约，再运行原生产物验收。

工具用法和隔离副本步骤见 [TLS 诊断说明](../../../tools/diagnostics/tls/README.md)。P10 暂保留原依赖；P11 搜索实验可以独立推进。
