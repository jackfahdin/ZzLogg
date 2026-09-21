# ZzLogg Windows 安装版在线更新——阶段 4 验收运行器。
# dot-source cases/*.ps1（每用例一个文件，文件名 NN-name.ps1 对应函数
# Invoke-Case_NN_name），逐个调用并收集结果对象
# @{ Name; Status = PASS|FAIL|SKIP; Evidence; ElapsedMs }。
# 退出码 = FAIL 数（SKIP 不算失败）。每个用例自清理（try/finally）。
# PowerShell 5.1/7 双兼容：只用标准 cmdlet。
#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SetupExe,        # 安装包路径；缺省时所有需要安装包的用例 SKIP
    [string]$InstallDir,      # 默认 "$env:ProgramFiles\ZzLogg"（CI 传显式路径）
    [string]$Filter = "*",    # 用例名通配
    [string]$ReportPath       # 可选，写 junit-ish 文本报告
)

$ErrorActionPreference = 'Stop'

if (-not $InstallDir) {
    $InstallDir = Join-Path $env:ProgramFiles 'ZzLogg'
}

# ---------- 共享上下文与助手（cases dot-source 后可见） ----------

$script:CaseContext = @{
    SetupExe        = $SetupExe
    SetupProvided   = -not [string]::IsNullOrWhiteSpace($SetupExe)
    SetupAvailable  = (-not [string]::IsNullOrWhiteSpace($SetupExe)) -and (Test-Path -LiteralPath $SetupExe)
    InstallDir      = $InstallDir
    ProgramDataRoot = Join-Path $env:ProgramData 'ZzLogg'
    TxRoot          = Join-Path $env:ProgramData 'ZzLogg\UpdateTransactions'
    UninstallKey    = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg'
    IsAdmin         = $false
}
try {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($identity)
    $script:CaseContext.IsAdmin = $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}
catch {
    $script:CaseContext.IsAdmin = $false
}

function Pass-Case {
    param([string]$Evidence)
    return @{ Status = 'PASS'; Evidence = $Evidence }
}

function Fail-Case {
    param([string]$Evidence)
    return @{ Status = 'FAIL'; Evidence = $Evidence }
}

function Skip-Case {
    param([string]$Evidence)
    return @{ Status = 'SKIP'; Evidence = $Evidence }
}

# 用例统一前置门禁：无安装包 → SKIP；显式给了假路径 → FAIL（失败语义不哑）；
# 非管理员 → SKIP（安装包 PrivilegesRequired=admin，非提权环境会触发 UAC 交互）。
# 返回 $null 表示可继续；否则返回应直接返回的结果对象。
function Test-CasePrereqs {
    param([Parameter(Mandatory = $true)]$Context)
    if (-not $Context.SetupProvided) {
        return (Skip-Case '未提供 -SetupExe：需要安装包的用例跳过（SKIP 不算失败）')
    }
    if (-not $Context.SetupAvailable) {
        return (Fail-Case "SetupExe 不存在: $($Context.SetupExe)")
    }
    if (-not $Context.IsAdmin) {
        return (Skip-Case '需管理员权限：安装包 PrivilegesRequired=admin，非提权环境会触发 UAC 交互')
    }
    return $null
}

# 运行安装包并等待，带超时保护（超时即杀进程，判定阻塞）。
function Invoke-SetupProcess {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [int]$TimeoutSec = 600
    )
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
    if ($process.WaitForExit($TimeoutSec * 1000)) {
        $process.Refresh()
        return @{ ExitCode = $process.ExitCode; TimedOut = $false }
    }
    try { $process.Kill() } catch { }
    return @{ ExitCode = $null; TimedOut = $true }
}

# Inno 静默安装；显式 /DIR= 保证 CI 安装到指定路径（受限入口禁止重定向）。
function Invoke-ZzLoggInstall {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [string]$TargetDir,
        [int]$TimeoutSec = 600
    )
    $target = if ($TargetDir) { $TargetDir } else { $Context.InstallDir }
    $defaultDir = Join-Path $env:ProgramFiles 'ZzLogg'
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ($target.TrimEnd('\') -ine $defaultDir.TrimEnd('\')) {
        $arguments += "/DIR=`"$target`""
    }
    $run = Invoke-SetupProcess -Exe $Context.SetupExe -Arguments $arguments -TimeoutSec $TimeoutSec
    return @{ Run = $run; TargetDir = $target }
}

# Inno 的卸载器位于安装目录；用户额外文件可使目录继续存在。
function Invoke-ZzLoggUninstall {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [int]$TimeoutSec = 120
    )
    $location = $null
    $item = Get-ItemProperty -Path $Context.UninstallKey -ErrorAction SilentlyContinue
    if ($item) { $location = $item.InstallLocation }
    if (-not $location) { return $true }
    # Read the actual numbered uninstaller, but never execute arbitrary registry commands.
    $match = [regex]::Match([string]$item.UninstallString, '^"(?<exe>[^"\r\n]+\\unins[0-9]{3}\.exe)"$', 'IgnoreCase')
    if (-not $match.Success) { return $false }
    $uninstaller = $match.Groups['exe'].Value
    if ([IO.Path]::GetDirectoryName($uninstaller).TrimEnd('\') -ine $location.TrimEnd('\')) { return $false }
    if (-not (Test-Path -LiteralPath $uninstaller)) { return $false }
    $process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -PassThru
    if (-not $process.WaitForExit($TimeoutSec * 1000) -or $process.ExitCode -ne 0) { return $false }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (((Test-Path -LiteralPath (Join-Path $location 'ZzLogg.exe')) -or (Test-Path -Path $Context.UninstallKey)) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    return (-not (Test-Path -LiteralPath (Join-Path $location 'ZzLogg.exe')) -and -not (Test-Path -Path $Context.UninstallKey))
}

# 仅当目录内有 .zzlogg-install-root 标记时才允许强制删除（防误删非安装目录）。
function Remove-ZzLoggDirGuarded {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (Test-Path -LiteralPath (Join-Path $Path '.zzlogg-install-root')) {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 目录内容指纹：相对路径 + 长度 + SHA-256 排序后再取总哈希；目录不存在返回 '<absent>'。
function Get-DirectoryFingerprint {
    param([Parameter(Mandatory = $true)][string]$Dir)
    if (-not (Test-Path -LiteralPath $Dir)) { return '<absent>' }
    $files = Get-ChildItem -LiteralPath $Dir -Recurse -File -Force | Sort-Object FullName
    $builder = New-Object System.Text.StringBuilder
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($Dir.Length)
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        [void]$builder.Append($relative).Append('|').Append($file.Length).Append('|').Append($hash).Append("`n")
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($builder.ToString()))
        return ([BitConverter]::ToString($digest) -replace '-', '')
    }
    finally {
        $sha.Dispose()
    }
}

# 受保护根 ACL 断言——与引擎 productionProtectedImage（txengine_win.cpp
# protectedRootAclShape）同一规则：
#   属主 ∈ {Administrators(S-1-5-32-544), SYSTEM(S-1-5-18)}；
#   仅检查 Allow ACE（引擎跳过非 ACCESS_ALLOWED 项）；
#   任何 Allow ACE 的掩码命中写位集合 FILE_WRITE_DATA|FILE_APPEND_DATA|
#   FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|DELETE|WRITE_DAC|WRITE_OWNER|
#   GENERIC_WRITE|GENERIC_ALL（0x500D0116）时，其主体必须是 Administrators/SYSTEM。
# 写位集合为显式数据写位枚举：不含 SYNCHRONIZE/READ_CONTROL（二者随
# FILE_GENERIC_WRITE 带入但并非写能力），故安装器授权的 Authenticated Users
# 只读 ACE（0x120089）通过断言；常量数值必须与引擎 protectedRootAclShape
# 逐位一致，引擎改动时同步本常量。
# Authenticated Users / Users 的只读形态由此规则蕴含（其 ACE 命中写位即违规）。
function Test-ProtectedRootAcl {
    param([Parameter(Mandatory = $true)][string]$Root)
    $writeBits = [uint32]0x500D0116
    $allowedWriters = @('S-1-5-18', 'S-1-5-32-544')
    $acl = Get-Acl -Path $Root

    $ownerSid = $null
    try {
        $ownerSid = ([System.Security.Principal.NTAccount]$acl.Owner).Translate(
            [System.Security.Principal.SecurityIdentifier]).Value
    }
    catch {
        return @{ Ok = $false; Detail = "属主 SID 无法解析: $($acl.Owner)" }
    }
    if ($allowedWriters -notcontains $ownerSid) {
        return @{ Ok = $false; Detail = "属主 $ownerSid 不在 {Administrators, SYSTEM} 集合内" }
    }

    foreach ($rule in $acl.Access) {
        if ($rule.AccessControlType -ne [System.Security.AccessControl.AccessControlType]::Allow) {
            continue
        }
        $mask = [uint32]([int]$rule.FileSystemRights)
        if (($mask -band $writeBits) -eq 0) { continue }
        $sid = $null
        try {
            $sid = $rule.IdentityReference.Translate(
                [System.Security.Principal.SecurityIdentifier]).Value
        }
        catch {
            return @{ Ok = $false; Detail = "ACE 主体无法解析: $($rule.IdentityReference)" }
        }
        if ($allowedWriters -notcontains $sid) {
            return @{ Ok = $false; Detail = "主体 $sid ($($rule.IdentityReference)) 持有写位 ACE（掩码 0x$($mask.ToString('X8'))），违反引擎受保护镜像断言" }
        }
    }

    $readerMasks = @()
    foreach ($rule in $acl.Access) {
        try {
            $sid = $rule.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value
        }
        catch { continue }
        if ($sid -eq 'S-1-5-11' -or $sid -eq 'S-1-5-32-545') {
            $readerMasks += "$($rule.IdentityReference)=$($rule.FileSystemRights)"
        }
    }
    return @{ Ok = $true; Detail = "属主 $ownerSid；写位 ACE 仅 Administrators/SYSTEM；只读主体: $($readerMasks -join '; ')" }
}

# ---------- 用例发现与执行 ----------

$caseDir = Join-Path $PSScriptRoot 'cases'
$caseFiles = Get-ChildItem -LiteralPath $caseDir -Filter '*.ps1' |
    Where-Object { $_.BaseName -like $Filter } |
    Sort-Object Name

if (-not $caseFiles) {
    Write-Output "没有匹配 -Filter '$Filter' 的用例"
    exit 0
}

$results = New-Object System.Collections.Generic.List[object]
foreach ($caseFile in $caseFiles) {
    $name = $caseFile.BaseName
    . $caseFile.FullName
    $functionName = 'Invoke-Case_' + ($name -replace '-', '_')
    $command = Get-Item -Path "function:$functionName" -ErrorAction SilentlyContinue
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $command) {
        $outcome = @{ Status = 'FAIL'; Evidence = "用例文件未定义函数 $functionName" }
    }
    else {
        try {
            $raw = @( & $functionName $script:CaseContext )
            $outcome = $raw | Where-Object { $_ -is [hashtable] -and $_.Status } | Select-Object -Last 1
            if (-not $outcome) {
                $outcome = @{ Status = 'FAIL'; Evidence = '用例未返回结果对象' }
            }
        }
        catch {
            $outcome = @{ Status = 'FAIL'; Evidence = "用例未处理异常: $($_.Exception.Message)" }
        }
    }
    $stopwatch.Stop()
    $result = [pscustomobject]@{
        Name      = $name
        Status    = [string]$outcome.Status
        Evidence  = [string]$outcome.Evidence
        ElapsedMs = [int64]$stopwatch.ElapsedMilliseconds
    }
    $results.Add($result)
    Write-Output ("CASE {0}: {1} - {2}" -f $result.Name, $result.Status, $result.Evidence)
}

$passCount = @($results | Where-Object Status -eq 'PASS').Count
$failCount = @($results | Where-Object Status -eq 'FAIL').Count
$skipCount = @($results | Where-Object Status -eq 'SKIP').Count
Write-Output ("汇总： {0} PASS / {1} FAIL / {2} SKIP" -f $passCount, $failCount, $skipCount)

if ($ReportPath) {
    $escape = { param([string]$s) [System.Security.SecurityElement]::Escape($s) }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(('<testsuite name="ZzLoggUpdateAcceptance" tests="{0}" failures="{1}" skipped="{2}">' -f $results.Count, $failCount, $skipCount))
    foreach ($result in $results) {
        $seconds = '{0:F3}' -f ($result.ElapsedMs / 1000.0)
        $lines.Add(('  <testcase classname="update-acceptance" name="{0}" time="{1}">' -f (& $escape $result.Name), $seconds))
        if ($result.Status -eq 'SKIP') {
            $lines.Add(('    <skipped message="{0}"/>' -f (& $escape $result.Evidence)))
        }
        elseif ($result.Status -eq 'FAIL') {
            $lines.Add(('    <failure message="{0}"/>' -f (& $escape $result.Evidence)))
        }
        $lines.Add(('    <system-out>{0}</system-out>' -f (& $escape $result.Evidence)))
        $lines.Add('  </testcase>')
    }
    $lines.Add('</testsuite>')
    $reportDir = Split-Path -Parent $ReportPath
    if ($reportDir -and -not (Test-Path -LiteralPath $reportDir)) {
        New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
    }
    [IO.File]::WriteAllLines($ReportPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "报告已写入: $ReportPath"
}

exit $failCount
