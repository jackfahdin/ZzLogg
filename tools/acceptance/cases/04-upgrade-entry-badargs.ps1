# 用例 04 upgrade-entry-badargs（CI 可跑）
# PASS 判据：`setup.exe /ZzLoggUpgrade=garbage`（及缺值、不存在路径两种变体）
# 退出码非 0；`$InstallDir` 内容哈希前后一致；无新增 staging 目录残留。
# 三个变体均为非法定位名（非 16 位小写 hex），在 .onInit 定位名校验处 Abort，
# 先于 VerifyTarget 与任何写操作；"不存在路径"变体取路径形垃圾值 `..\nonexistent`
# （合法但无对应事务的定位名会进入升级流程并产生 staging，不属于坏参数变体）。
function Invoke-Case_04_upgrade_entry_badargs {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $txRoot = $Context.TxRoot
    $variants = @(
        @{ Label = '垃圾定位名'; Arguments = @('/S', '/ZzLoggUpgrade=garbage') },
        @{ Label = '缺值'; Arguments = @('/S', '/ZzLoggUpgrade=') },
        @{ Label = '不存在路径'; Arguments = @('/S', '/ZzLoggUpgrade=..\nonexistent') }
    )

    try {
        [void](Invoke-ZzLoggUninstall $Context)
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }

        $stagingBefore = @()
        if (Test-Path -LiteralPath $txRoot) {
            $stagingBefore = @(Get-ChildItem -LiteralPath $txRoot -Force -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty Name)
        }
        $hashBefore = Get-DirectoryFingerprint $Context.InstallDir

        $failures = @()
        foreach ($variant in $variants) {
            # /ZzLoggUpgrade= 必须为最后一个参数（缺值变体依赖命令行以其结尾）
            $run = Invoke-SetupProcess -Exe $Context.SetupExe -Arguments $variant.Arguments -TimeoutSec 120
            if ($run.TimedOut) {
                $failures += "变体[$($variant.Label)] 超时阻塞（疑似弹窗），进程被终止"
            }
            elseif ($run.ExitCode -eq 0) {
                $failures += "变体[$($variant.Label)] 退出码为 0，期望非 0"
            }
        }

        $hashAfter = Get-DirectoryFingerprint $Context.InstallDir
        if ($hashBefore -ne $hashAfter) {
            $failures += 'InstallDir 内容哈希前后不一致'
        }
        $stagingAfter = @()
        if (Test-Path -LiteralPath $txRoot) {
            $stagingAfter = @(Get-ChildItem -LiteralPath $txRoot -Force -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty Name)
        }
        $newEntries = @($stagingAfter | Where-Object { $stagingBefore -notcontains $_ })
        if ($newEntries) {
            $failures += "UpdateTransactions 下新增残留: $($newEntries -join ', ')"
        }

        if ($failures) {
            return (Fail-Case ($failures -join '；'))
        }
        return (Pass-Case '三个坏参数变体（垃圾定位名/缺值/不存在路径）退出码均非 0；InstallDir 内容哈希前后一致；UpdateTransactions 无新增 staging 残留')
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
