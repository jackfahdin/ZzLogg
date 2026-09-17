# 用例 05 recover-entry-nojournal（CI 可跑）
# PASS 判据：无 journal.log 时 `setup.exe /ZzLoggRecover=<dir>` 退出码非 0，
# 且不改动 `$InstallDir`。
# 定位名取随机 16 位小写 hex（非零），保证受保护根下不存在对应事务目录与
# journal.log；恢复入口在 zzlogg_recover_missing 处 Abort，不产生任何写入。
function Invoke-Case_05_recover_entry_nojournal {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    try {
        [void](Invoke-ZzLoggUninstall $Context)
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }

        $locator = $null
        for ($attempt = 0; $attempt -lt 8; ++$attempt) {
            $candidate = (([Guid]::NewGuid().ToString('N')).Substring(0, 16))
            if ($candidate -match '0' -and $candidate -notmatch '[1-9a-f]') { continue }
            if (-not (Test-Path -LiteralPath (Join-Path $Context.TxRoot "$candidate\journal.log"))) {
                $locator = $candidate
                break
            }
        }
        if (-not $locator) {
            return (Skip-Case '无法构造无 journal 的随机定位名（重试 8 次均冲突）')
        }

        $hashBefore = Get-DirectoryFingerprint $Context.InstallDir
        $txDir = Join-Path $Context.TxRoot $locator

        $run = Invoke-SetupProcess -Exe $Context.SetupExe `
            -Arguments @('/S', "/ZzLoggRecover=$locator") -TimeoutSec 120

        $failures = @()
        if ($run.TimedOut) {
            $failures += '恢复入口超时阻塞（疑似弹窗），进程被终止'
        }
        elseif ($run.ExitCode -eq 0) {
            $failures += '退出码为 0，期望非 0（无 journal.log 的恢复必须被拒绝）'
        }
        $hashAfter = Get-DirectoryFingerprint $Context.InstallDir
        if ($hashBefore -ne $hashAfter) {
            $failures += 'InstallDir 内容哈希前后不一致'
        }
        if (Test-Path -LiteralPath $txDir) {
            $failures += "恢复入口创建了事务目录残留: $txDir"
        }

        if ($failures) {
            return (Fail-Case ($failures -join '；'))
        }
        return (Pass-Case "无 journal.log 时 /ZzLoggRecover=$locator 退出码 $($run.ExitCode)（非 0）；InstallDir 内容哈希前后一致；无事务目录残留")
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
