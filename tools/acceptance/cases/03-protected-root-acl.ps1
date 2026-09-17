# 用例 03 protected-root-acl（CI 可跑，管理员）
# PASS 判据：`%ProgramData%\ZzLogg\UpdateTransactions` 存在；Get-Acl 显示属主
# ∈ {Administrators, SYSTEM}；除 Administrators/SYSTEM 外无任何写位 ACE；
# Authenticated Users 或 Users 仅只读——与引擎断言同一规则
# （引擎 productionProtectedImage，src/updater/txengine_win.cpp：
# 属主 Administrators/SYSTEM；仅检查 Allow ACE；写位集合
# FILE_GENERIC_WRITE|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL）。
# 受保护根由安装器受限升级入口在首次创建时加固（icacls /inheritance:r +
# Administrators/SYSTEM 完全 + Authenticated Users 只读），故本用例以
# `/ZzLoggUpgrade=<定位名>` 触发根创建；引擎随后因无协调者凭据文件失败关闭，
# 这是预期行为（本用例不断言该入口退出码，只断言根 ACL 形状）。
function Invoke-Case_03_protected_root_acl {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    $locator = '0000000000000001'
    $txRoot = $Context.TxRoot
    $rootExistedBefore = Test-Path -LiteralPath $txRoot
    $stagingDir = Join-Path $txRoot "staging-$locator"
    $txDir = Join-Path $txRoot $locator

    try {
        [void](Invoke-ZzLoggUninstall $Context)
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }

        $entry = Invoke-SetupProcess -Exe $Context.SetupExe `
            -Arguments @('/S', "/ZzLoggUpgrade=$locator") -TimeoutSec 120

        if (-not (Test-Path -LiteralPath $txRoot)) {
            return (Fail-Case "受限升级入口运行后受保护根仍不存在: $txRoot（入口 exit=$($entry.ExitCode) timeout=$($entry.TimedOut)）")
        }

        $acl = Test-ProtectedRootAcl -Root $txRoot
        if (-not $acl.Ok) {
            return (Fail-Case "受保护根 ACL 违反引擎断言规则: $($acl.Detail)")
        }

        return (Pass-Case "UpdateTransactions 存在；ACL 形状与引擎断言逐条一致：$($acl.Detail)（升级入口 exit=$($entry.ExitCode)，引擎无凭据失败关闭属预期）")
    }
    finally {
        Remove-Item -LiteralPath $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $txDir -Recurse -Force -ErrorAction SilentlyContinue
        if (-not $rootExistedBefore -and (Test-Path -LiteralPath $txRoot)) {
            $leftover = Get-ChildItem -LiteralPath $txRoot -Force -ErrorAction SilentlyContinue
            if (-not $leftover) {
                Remove-Item -LiteralPath $txRoot -Force -ErrorAction SilentlyContinue
            }
        }
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
