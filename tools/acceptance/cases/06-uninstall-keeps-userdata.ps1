# 用例 06 uninstall-keeps-userdata（CI 可跑）
# 卸载删除安装器所属文件，保留 AppData 配置及安装目录中用户自行添加的文件。
function Invoke-Case_06_uninstall_keeps_userdata {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $userDir = Join-Path $env:APPDATA 'ZzLogg'
    $userDirExistedBefore = Test-Path -LiteralPath $userDir
    $saved = @{}
    $probes = @(
        (Join-Path $userDir 'ZzLogg.ini'),
        (Join-Path $userDir 'ZzLogg_session.ini'),
        (Join-Path $Context.InstallDir ('acceptance-user-' + [Guid]::NewGuid().ToString('N') + '.log'))
    )
    $probeText = "[acceptance-probe]`r`ncase=06`r`n"
    try {
        if (-not (Invoke-ZzLoggUninstall $Context)) { return (Fail-Case '前置卸载失败') }
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }
        New-Item -ItemType Directory -Path $userDir -Force | Out-Null
        foreach ($probe in $probes) {
            $saved[$probe] = if (Test-Path -LiteralPath $probe) { [IO.File]::ReadAllBytes($probe) } else { $null }
            [IO.File]::WriteAllText($probe, $probeText)
        }
        $uninstalled = Invoke-ZzLoggUninstall $Context
        $failures = @()
        foreach ($probe in $probes) {
            if (-not (Test-Path -LiteralPath $probe) -or [IO.File]::ReadAllText($probe) -cne $probeText) {
                $failures += "静默卸载后用户文件丢失或被修改: $probe"
            }
        }
        if (-not $uninstalled) { $failures += '静默卸载后程序文件或卸载登记仍存在' }
        if ($failures) { return (Fail-Case ($failures -join '；')) }
        return (Pass-Case '卸载后程序与登记已移除；AppData 配置、会话和安装目录用户文件完整保留')
    }
    finally {
        foreach ($probe in @($saved.Keys)) {
            if ($null -ne $saved[$probe]) { [IO.File]::WriteAllBytes($probe, [byte[]]$saved[$probe]) }
            else { Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue }
        }
        [void](Invoke-ZzLoggUninstall $Context)
        foreach ($dir in @($Context.InstallDir, $userDir)) {
            if ($dir -eq $userDir -and $userDirExistedBefore) { continue }
            if ((Test-Path -LiteralPath $dir) -and -not (Get-ChildItem -LiteralPath $dir -Force)) {
                Remove-Item -LiteralPath $dir -Force
            }
        }
    }
}
