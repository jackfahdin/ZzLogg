# Windows TLS 运行依赖诊断

`zzlogg_tls_probe` 是显式工具，不进入应用、安装包或默认 `ci_build`。它使用真实 Qt Network 请求，记录 TLS 后端、版本、插件路径、Windows 加载模块、最终 URL、证书错误及内容 SHA-256。应用目前继续使用原有打包依赖；本工具不能独立证明可以删除 OpenSSL。

## 构建与本地工具验证

在已启用 `KLOGG_BUILD_UI_TESTS=ON` 的构建目录执行：

```bash
cmake --build out/build/p6-boolean --target zzlogg_tls_probe --parallel 8
out/build/p6-boolean/output/zzlogg_tls_probe --inspect
python3 tests/tools/test_tls_probe.py
```

测试默认使用上面的可执行文件；其他目录通过 `ZZLOGG_TLS_PROBE` 指定绝对路径。测试需要 Python 3 和 `openssl` 命令，只监听本机临时端口、生成临时证书，不修改系统信任库、不访问公网。覆盖内容成功／哈希失败、信任失败、主机名错误、带响应体的 HTTPS 重定向、降级拒绝、重定向循环、CONNECT 代理以及超时／连接失败不能冒充证书拒绝。它不是默认 CI 测试，执行者需显式构建工具并运行。

## 请求与判定

- `--inspect`：只枚举能力；即使 `passed=true`，也没有验证 HTTPS 请求。
- `--backend schannel`：在创建网络对象前选择后端，不可用时失败；省略则观察 Qt 默认选择。
- `--url <https-url> --sha256 <64位十六进制>`：只有严格证书验证、最终 HTTP 200、HTTPS 和完整内容哈希均正确才通过；哈希应来自独立验证过的固定测试内容。
- `--url <证书错误的https-url> --expect-tls-error`：必须同时观察到证书错误和 TLS 握手失败；DNS、连接拒绝、超时、HTTP 错误均不算通过。
- `--proxy system`（默认）：与应用一样启用系统代理配置；`direct` 禁用代理；`http://host:port` 测显式 CONNECT。工具不提供代理凭据输入，企业代理认证、PAC 等仍需实际应用验证。
- `--ca-file <pem>`：仅给本次请求追加测试 CA，仍验证证书与主机名，不安装根证书。系统信任库验收时不要使用此选项。
- `--isolated-plugins`：只从工具所在目录发现 Qt 插件；不是 DLL 沙箱，Windows 还需限制 PATH 并检查 `loaded_modules`。
- `--timeout-ms <1..120000>`：整个请求的总期限，默认 15000 ms，最多跟随 5 次重定向。

退出码非零表示不符合预期。JSON 写 stdout，Qt 插件／网络诊断写 stderr；两者都应保存。`ssl_runtime_version` 在 Schannel 下可能为空，不能据此判为 TLS 不可用。`loaded_modules` 仅在 Windows 提供；Windows 空列表意味着未拿到模块证据，不可放行删库。

## Windows 原生产物对照

使用与产物**同一 Qt kit、架构和配置**构建 `zzlogg_tls_probe`（例如 `cmake --build out/build/windows-msvc-release --config RelWithDebInfo --target zzlogg_tls_probe`）。分别对实际安装目录和最终便携目录执行下列步骤，不能只测开发机 Qt 的 bin 目录。

在 PowerShell 7 中先设置 `$sourceRuntime` 为待测产物目录、`$builtProbe` 为探针 EXE 绝对路径。将 `$verifiedUrl`、`$sha256` 设置为使用系统信任证书的固定测试资源及独立确认的 SHA-256，将 `$badCertificateUrl` 设置为可达但证书错误的受控 HTTPS 地址。使用不包含敏感查询参数的测试 URL，报告会记录 URL。以下命令只操作新建副本：

```powershell
$auditRoot = Join-Path $env:TEMP ("zzlogg-tls-" + [guid]::NewGuid())
New-Item -ItemType Directory $auditRoot | Out-Null
foreach ($variant in @('original', 'without-openssl')) {
    $folder = Join-Path $auditRoot $variant
    Copy-Item -LiteralPath $sourceRuntime -Destination $folder -Recurse
    Copy-Item -LiteralPath $builtProbe -Destination (Join-Path $folder 'zzlogg_tls_probe.exe')
    # Override any development-kit prefix in the copied qt.conf only.
    "[Paths]`nPrefix=.`nPlugins=." | Set-Content (Join-Path $folder 'qt.conf')
    if ($variant -eq 'without-openssl') {
        Get-ChildItem $folder -Recurse -File | Where-Object {
            $_.Name -match '^(libssl.*|libcrypto.*|ssleay32|libeay32|qopensslbackendd?)\.dll$'
        } | Remove-Item
    }
    Get-ChildItem $folder -Recurse -File | ForEach-Object {
        [pscustomobject]@{ Path = $_.FullName; Bytes = $_.Length;
            FileVersion = $_.VersionInfo.FileVersion;
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $auditRoot "$variant-inventory.json")
}

$oldPath = $env:PATH
$oldPlugins = $env:QT_PLUGIN_PATH
try {
    Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
    foreach ($variant in @('original', 'without-openssl')) {
        $folder = Join-Path $auditRoot $variant
        $exe = Join-Path $folder 'zzlogg_tls_probe.exe'
        $env:PATH = "$folder;$env:SystemRoot\System32;$env:SystemRoot"
        foreach ($backend in @('default', 'schannel')) {
            $options = @('--isolated-plugins', '--proxy', 'system')
            if ($backend -ne 'default') { $options += @('--backend', $backend) }
            $name = "$variant-$backend"
            & $exe @options --url $verifiedUrl --sha256 $sha256 `
                2> (Join-Path $auditRoot "$name-success.stderr.txt") |
                Set-Content -Encoding utf8 (Join-Path $auditRoot "$name-success.json")
            if ($LASTEXITCODE -ne 0) { throw "HTTPS failed: $name; see $auditRoot" }
            & $exe @options --url $badCertificateUrl --expect-tls-error `
                2> (Join-Path $auditRoot "$name-cert.stderr.txt") |
                Set-Content -Encoding utf8 (Join-Path $auditRoot "$name-cert.json")
            if ($LASTEXITCODE -ne 0) { throw "Certificate rejection failed: $name; see $auditRoot" }
        }
    }
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPlugins
}
Write-Output "Audit artifacts: $auditRoot"
```

若某个后端缺失、DLL 缺失、证书／代理问题导致失败，保留报告并处理原因，不跳过失败继续宣布可删库。检查四组 JSON：无 OpenSSL 副本中应实际选择 `schannel`，实际加载 `qschannelbackend.dll`，且没有加载任何 OpenSSL DLL；Qt DLL 与 TLS 插件必须来自该副本。检查 `loaded_modules` 是否借用了开发机其他目录，系统 DLL 来自 Windows 目录属正常。模块枚举失败或异常路径需要调查，不能只看文件清单。

还需用固定内容的 HTTPS 重定向地址重复成功用例，并以 `direct`、实际系统代理、必要时显式 HTTP 代理分别运行；HTTPS→HTTP 降级不应成功。企业证书链、代理认证及系统信任库与 Linux 的临时证书测试不同。

最终在副本中启动实际应用，保持证书验证开启，完成更新清单获取、带大小与哈希校验的安装包下载和远程日志下载。探针不会验证清单签名、主机白名单、安装包缓存、UI 错误展示或应用的重定向策略，也不会安装下载的包。只有这些原生验收均满足，才可修改正式打包脚本及其契约断言。

审计结论与状态见 [P10 记录](../../../docs/superpowers/plans/2026-11-16-windows-tls-audit.md)。
