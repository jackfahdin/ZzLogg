# 用例 04 upgrade-entry-badargs（CI 可跑）
# PASS 判据：`setup.exe /ZzLoggUpgrade=garbage`（及缺值、不存在路径两种变体）
# 退出码非 0；`$InstallDir` 内容哈希前后一致；无新增 staging 目录残留。
# 非法定位名和冲突参数均在 InitializeSetup 中拒绝，
# 先于 VerifyTarget 与任何写操作；"不存在路径"变体取路径形垃圾值 `..\nonexistent`
# （合法但无对应事务的定位名会进入升级流程并产生 staging，不属于坏参数变体）。
function Invoke-Case_04_upgrade_entry_badargs {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $txRoot = $Context.TxRoot
    $variants = @(
        @{ Label = '垃圾定位名'; Arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ZzLoggUpgrade=garbage') },
        @{ Label = '缺值'; Arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ZzLoggUpgrade=') },
        @{ Label = '不存在路径'; Arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ZzLoggUpgrade=..\nonexistent') },
        @{ Label = 'Inno 目录覆盖'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade=0123456789abcdef', '/DIR=C:\unexpected') },
        @{ Label = '旧目录覆盖'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade=0123456789abcdef', '/D=C:\unexpected') },
        @{ Label = '重复入口'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade=0123456789abcdef', '/ZzLoggUpgrade=0123456789abcdef') },
        @{ Label = '混合入口'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade=0123456789abcdef', '/ZzLoggRecover=0123456789abcdef') },
        @{ Label = '全零定位名'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade=0000000000000000') },
        @{ Label = '缺少等号'; Arguments = @('/VERYSILENT', '/ZzLoggUpgrade') },
        @{ Label = '无静默参数的受限入口'; Arguments = @('/ZzLoggUpgrade=garbage') }
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
            elseif ($run.ExitCode -ne 2) {
                $failures += "变体[$($variant.Label)] 退出码 $($run.ExitCode)，期望 2"
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
        return (Pass-Case '所有坏参数及冲突入口均拒绝；InstallDir 内容哈希前后一致；UpdateTransactions 无新增 staging 残留')
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
