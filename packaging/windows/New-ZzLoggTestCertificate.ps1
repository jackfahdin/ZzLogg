<#
.SYNOPSIS
    生成 ZzLogg 测试签名自签名代码签名证书。

.DESCRIPTION
    本脚本仅用于测试签名，发布签名由生产流水线与真实证书承担。
    生成的证书 Subject 含 "Test" 与 "NOT FOR PRODUCTION" 字样，仅写入
    Cert:\CurrentUser\*（绝不写 LocalMachine）。
    重复运行时若同 Subject 证书已存在则复用并打印指纹（幂等）。

.PARAMETER ExportPfx
    可选。将证书（含私钥）导出为 PFX 到该路径。PFX 属私钥材料，绝不入库。

.PARAMETER Password
    导出 PFX 时必填。PFX 保护口令。

.PARAMETER TrustCurrentUser
    开关。将证书导入 Cert:\CurrentUser\Root，使本机 signtool verify /pa 验签通过。
    验证结束后务必删除该根证书（Remove-Item Cert:\CurrentUser\Root\<指纹>），
    避免长期信任一个无保护的测试根。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File New-ZzLoggTestCertificate.ps1 -TrustCurrentUser
#>
[CmdletBinding()]
param(
    [string]$ExportPfx,
    [SecureString]$Password,
    [switch]$TrustCurrentUser
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$subject = 'CN=ZzLogg Test Code Signing (NOT FOR PRODUCTION)'

if ($ExportPfx -and -not $Password) {
    throw "导出 PFX（-ExportPfx）时必须提供 -Password（SecureString）。"
}
if ($Password -and -not $ExportPfx) {
    throw "-Password 仅在 -ExportPfx 导出时有效。"
}

$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if ($cert) {
    Write-Host "已存在同 Subject 测试证书，复用（幂等）。"
}
else {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
        -Subject $subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(2) `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
    Write-Host "已新建测试签名证书。"
}

if ($TrustCurrentUser) {
    $cerPath = Join-Path $env:TEMP 'zzlogg-test-root.cer'
    try {
        Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null
        Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
        Write-Host "已导入 Cert:\CurrentUser\Root（本机验签信任）。验证结束后请删除：Remove-Item Cert:\CurrentUser\Root\$($cert.Thumbprint)"
    }
    finally {
        Remove-Item $cerPath -Force -ErrorAction SilentlyContinue
    }
}

if ($ExportPfx) {
    Export-PfxCertificate -Cert $cert -FilePath $ExportPfx -Password $Password -Force | Out-Null
    Write-Host "已导出 PFX：$ExportPfx（私钥材料，绝不入库；.gitignore 已覆盖 *.pfx）。"
}

Write-Host ""
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host "Subject:    $($cert.Subject)"
Write-Host "=============================================================="
Write-Host "警告：本证书仅测试用途，禁止用于发布！"
Write-Host "发布签名由生产流水线与真实证书承担。"
Write-Host "=============================================================="
