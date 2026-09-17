# 用例 07 space-preflight-multivolume（CI 可跑，runner 有 C:/D: 双卷）
# PASS 判据：用 `/D=` 装到 D: 卷（普通安装不禁 /D=，仅受限入口禁）→ 制造暂存
# （C: ProgramData）与目标（D:）跨卷场景，断言安装成功且预检日志无空间误报；
# 单卷机器上 SKIP。
# 说明：普通安装本身不跑引擎空间预检（预检属于升级事务，需协调者凭据，见用例 08），
# 故"无空间误报"断言为：跨卷安装成功 + %ProgramData%\ZzLogg 下无报告空间不足的日志。
function Invoke-Case_07_space_preflight_multivolume {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $systemDrive = $env:SystemDrive.TrimEnd('\')
    $fixedDisks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' |
        Select-Object -ExpandProperty DeviceID)
    $otherDisks = @($fixedDisks | Where-Object { $_ -ne $systemDrive })
    if ($otherDisks.Count -lt 1) {
        return (Skip-Case "单卷机器（固定卷: $($fixedDisks -join ', ')），无法构造暂存（$systemDrive ProgramData）与目标跨卷场景；需在双卷 runner 上执行")
    }

    $targetVolume = $otherDisks[0]
    $targetDir = "$targetVolume\ZzLoggAcceptance"

    try {
        [void](Invoke-ZzLoggUninstall $Context)

        $logStart = Get-Date
        $install = Invoke-ZzLoggInstall $Context -TargetDir $targetDir
        if ($install.Run.TimedOut) {
            return (Fail-Case "跨卷安装超时（/D=$targetDir），进程被终止")
        }
        if ($install.Run.ExitCode -ne 0) {
            return (Fail-Case "跨卷安装退出码 $($install.Run.ExitCode)，期望 0（/D=$targetDir）")
        }
        foreach ($name in @('ZzLogg.exe', '.zzlogg-install-root', '.zzlogg-files.manifest')) {
            if (-not (Test-Path -LiteralPath (Join-Path $targetDir $name))) {
                return (Fail-Case "跨卷安装后 $targetDir 缺少 $name")
            }
        }

        $spaceAlarms = @()
        $programData = $Context.ProgramDataRoot
        if (Test-Path -LiteralPath $programData) {
            $logs = Get-ChildItem -LiteralPath $programData -Recurse -Filter '*.log' -Force -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTime -ge $logStart.AddMinutes(-1) }
            foreach ($log in $logs) {
                $hits = Select-String -LiteralPath $log.FullName `
                    -Pattern 'insufficient', 'space', '空间不足' -ErrorAction SilentlyContinue
                if ($hits) {
                    $spaceAlarms += "$($log.FullName): $($hits[0].Line.Trim())"
                }
            }
        }
        if ($spaceAlarms) {
            return (Fail-Case "预检日志出现空间误报: $($spaceAlarms -join '；')")
        }

        return (Pass-Case "跨卷安装成功（暂存卷 $systemDrive ProgramData / 目标卷 $targetVolume，/D=$targetDir 退出 0，落地标记与清单齐全）；%ProgramData%\ZzLogg 无空间预检误报日志")
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $targetDir
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
