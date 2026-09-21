# 用例 13：模拟 NSIS 已登记安装，验证 Inno 默认沿用非标准目录并复用卸载日志。
# 此夹具覆盖登记兼容；真实旧版二进制→新版仍需 VM 验收。
function Invoke-Case_13_migrate_legacy_registration {
    param($Context)
    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }
    if (-not (Invoke-ZzLoggUninstall $Context)) { return (Fail-Case '前置卸载失败') }
    $target = Join-Path $env:ProgramFiles ('ZzLogg migration ' + [Guid]::NewGuid().ToString('N'))
    $probe = Join-Path $target 'user-added.log'
    $legacy = Join-Path $target 'Uninstall.exe'
    $userText = 'Must survive migration and uninstall'
    try {
        New-Item -ItemType Directory -Path $target | Out-Null
        # 不可执行的旧卸载器：任何尝试执行它都必须导致此用例失败。
        [IO.File]::WriteAllText($legacy, 'legacy uninstaller must never run')
        [IO.File]::WriteAllText($probe, $userText)
        [IO.File]::WriteAllText((Join-Path $target '.zzlogg-install-root'), "ZzLogg 26.09.01`r`n")
        New-Item -Path $Context.UninstallKey -Force | Out-Null
        foreach ($item in @{
            DisplayName = 'ZzLogg'; DisplayVersion = '26.09.01';
            InstallLocation = $target; UninstallString = ('"' + $legacy + '"')
        }.GetEnumerator()) {
            New-ItemProperty -Path $Context.UninstallKey -Name $item.Key -Value $item.Value -PropertyType String -Force | Out-Null
        }
        New-ItemProperty -Path $Context.UninstallKey -Name UpdateIdentitySchema -Value 2 -PropertyType DWord -Force | Out-Null
        for ($attempt = 1; $attempt -le 2; $attempt++) {
            # 无 /DIR：验证已登记目录探测；第二次覆盖验证 Inno 日志可以追加。
            $run = Invoke-SetupProcess -Exe $Context.SetupExe -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -TimeoutSec 600
            if ($run.TimedOut -or $run.ExitCode -ne 0) { return (Fail-Case "第 $attempt 次覆盖安装失败: $($run.ExitCode)") }
            $registration = Get-ItemProperty -Path $Context.UninstallKey
            if ($registration.InstallLocation.TrimEnd('\') -ine $target.TrimEnd('\')) { return (Fail-Case '覆盖安装未沿用旧登记目录') }
            if (-not (Test-Path -LiteralPath (Join-Path $target 'ZzLogg.exe'))) { return (Fail-Case '旧登记目录中缺少新程序') }
            if (Test-Path -LiteralPath $legacy) { return (Fail-Case '遗留 NSIS 卸载器未移除') }
            if ([IO.File]::ReadAllText($probe) -cne $userText) { return (Fail-Case '覆盖安装修改了用户文件') }
            $innoLogs = @(Get-ChildItem -LiteralPath $target -Filter 'unins*.dat')
            if ($innoLogs.Count -ne 1) { return (Fail-Case '覆盖安装未复用同一份 Inno 卸载日志') }
            if (Test-Path -Path ($Context.UninstallKey + '_is1')) { return (Fail-Case '存在重复卸载登记') }
        }
        if (-not (Invoke-ZzLoggUninstall $Context)) { return (Fail-Case '迁移后的卸载失败') }
        if (-not (Test-Path -LiteralPath $probe) -or [IO.File]::ReadAllText($probe) -cne $userText) { return (Fail-Case '卸载删除或修改了额外用户文件') }
        return (Pass-Case '旧登记目录自动沿用，重复覆盖复用卸载日志，无重复登记，卸载保留用户文件')
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        # 仅清理本用例独占随机目录及仍指向此目录的登记。
        $registration = Get-ItemProperty -Path $Context.UninstallKey -ErrorAction SilentlyContinue
        if ($registration -and $registration.InstallLocation -eq $target) { Remove-Item -Path $Context.UninstallKey -Recurse -Force }
        Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
    }
}
