<#
.SYNOPSIS
    使用 signtool 对 ZzLogg 构建产物做测试签名 / 验签。

.DESCRIPTION
    本脚本仅用于测试签名，发布签名由生产流水线与真实证书承担。
    签名对象仅限 .exe / .dll / .msi，其余扩展名一律拒绝（防误签）。
    signtool 定位顺序：PATH → vswhere 探测 → Windows Kits 10 bin 最新版；
    找不到时报错并以退出码 2 退出，提示安装 Windows SDK。

.PARAMETER Files
    待签名/验签的文件路径列表。

.PARAMETER PfxPath
    签名所用 PFX 路径（与 -Thumbprint 二选一）。

.PARAMETER Thumbprint
    签名所用证书指纹（Cert:\CurrentUser\My 中的证书；与 -PfxPath 二选一）。

.PARAMETER Password
    PFX 保护口令（使用 -PfxPath 时必填）。

.PARAMETER TimestampUrl
    可选。RFC3161 时间戳服务地址；默认不加时间戳以支持离线。

.PARAMETER Verify
    开关。只验签不签名（signtool verify /pa；需测试根已导入 CurrentUser\Root）。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Sign-ZzLoggArtifacts.ps1 -Files .\ZzLoggUpdateTx.exe -Thumbprint <指纹>
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Sign-ZzLoggArtifacts.ps1 -Files .\ZzLoggUpdateTx.exe -Verify
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]]$Files,
    [string]$PfxPath,
    [string]$Thumbprint,
    [SecureString]$Password,
    [string]$TimestampUrl,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$allowedExtensions = @('.exe', '.dll', '.msi')

foreach ($file in $Files) {
    $ext = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
    if ($allowedExtensions -notcontains $ext) {
        throw "拒绝签名/验签：'$file' 扩展名 '$ext' 不在允许列表（$($allowedExtensions -join ', ')）。"
    }
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "文件不存在：'$file'。"
    }
}

if (-not $Verify) {
    if ($PfxPath -and $Thumbprint) {
        throw "-PfxPath 与 -Thumbprint 二选一，不可同时指定。"
    }
    if (-not $PfxPath -and -not $Thumbprint) {
        throw "签名模式必须指定 -PfxPath 或 -Thumbprint 之一（或使用 -Verify 仅验签）。"
    }
    if ($PfxPath -and -not $Password) {
        throw "使用 -PfxPath 时必须提供 -Password（SecureString）。"
    }
    if ($PfxPath -and -not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
        throw "PFX 不存在：'$PfxPath'。"
    }
}

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($installPath) {
            $found = Get-ChildItem -LiteralPath $installPath -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.Directory.Name -eq 'x64' } |
                Select-Object -First 1
            if (-not $found) {
                $found = Get-ChildItem -LiteralPath $installPath -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
                    Select-Object -First 1
            }
            if ($found) { return $found.FullName }
        }
    }

    $kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $kitsBin) {
        $found = Get-ChildItem -Path (Join-Path $kitsBin '*\x64\signtool.exe') -ErrorAction SilentlyContinue |
            Sort-Object {
                $name = $_.Directory.Parent.Name
                $v = $null
                if ([version]::TryParse($name, [ref]$v)) { $v } else { [version]'0.0' }
            } -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }

    return $null
}

$signtool = Find-SignTool
if (-not $signtool) {
    [Console]::Error.WriteLine("未找到 signtool.exe（已探测 PATH、vswhere、Windows Kits 10 bin）。请安装 Windows SDK 后重试。")
    exit 2
}
Write-Host "signtool: $signtool"

$failed = @()

if ($Verify) {
    foreach ($file in $Files) {
        & $signtool verify /pa $file | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[验签通过] $file"
        }
        else {
            Write-Host "[验签失败] $file (signtool 退出码 $LASTEXITCODE)"
            $failed += $file
        }
    }
}
else {
    $signArgs = @('sign', '/fd', 'SHA256')
    if ($TimestampUrl) {
        $signArgs += @('/tr', $TimestampUrl, '/td', 'SHA256')
    }
    if ($PfxPath) {
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
            [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
        $signArgs += @('/f', $PfxPath, '/p', $plain)
    }
    else {
        $signArgs += @('/sha1', $Thumbprint)
    }

    foreach ($file in $Files) {
        & $signtool @signArgs $file | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[签名成功] $file"
        }
        else {
            Write-Host "[签名失败] $file (signtool 退出码 $LASTEXITCODE)"
            $failed += $file
        }
    }
}

if ($failed.Count -gt 0) {
    [Console]::Error.WriteLine("以下文件处理失败：")
    $failed | ForEach-Object { [Console]::Error.WriteLine("  $_") }
    exit 1
}
exit 0
