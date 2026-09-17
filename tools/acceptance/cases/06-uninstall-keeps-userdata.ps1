# 用例 06 uninstall-keeps-userdata（CI 可跑）
# PASS 判据：安装→写入 `%APPDATA%\ZzLogg\probe.ini`→静默卸载→`probe.ini` 仍在；
# `$InstallDir` 已删除。
# 依据：卸载器只删除 ZzLogg.ini / ZzLogg_session.ini 并以非 /r 的 RMDir 尝试移除
# 用户配置目录，probe.ini 使目录非空，RMDir 静默失败，用户数据保留。
function Invoke-Case_06_uninstall_keeps_userdata {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $userDir = Join-Path $env:APPDATA 'ZzLogg'
    $probe = Join-Path $userDir 'probe.ini'
    $userDirExistedBefore = Test-Path -LiteralPath $userDir

    try {
        [void](Invoke-ZzLoggUninstall $Context)
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }

        if (-not (Test-Path -LiteralPath $userDir)) {
            New-Item -ItemType Directory -Path $userDir -Force | Out-Null
        }
        [IO.File]::WriteAllText($probe, "[acceptance-probe]`r`ncase=06`r`n")

        $uninstalled = Invoke-ZzLoggUninstall $Context

        $failures = @()
        if (-not (Test-Path -LiteralPath $probe)) {
            $failures += "静默卸载后 probe.ini 丢失: $probe"
        }
        if (-not $uninstalled -or (Test-Path -LiteralPath $Context.InstallDir)) {
            $failures += "静默卸载后 InstallDir 仍存在: $($Context.InstallDir)"
        }

        if ($failures) {
            return (Fail-Case ($failures -join '；'))
        }
        return (Pass-Case '安装→写入 probe.ini→静默卸载后 probe.ini 仍在；InstallDir 已删除')
    }
    finally {
        Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
        if (-not $userDirExistedBefore -and (Test-Path -LiteralPath $userDir)) {
            $leftover = Get-ChildItem -LiteralPath $userDir -Force -ErrorAction SilentlyContinue
            if (-not $leftover) {
                Remove-Item -LiteralPath $userDir -Force -ErrorAction SilentlyContinue
            }
        }
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
