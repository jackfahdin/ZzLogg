# 用例 02 registry-identity（CI 可跑）
# PASS 判据：`HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZzLogg` 存在且
# `UpdateIdentitySchema`=2（DWORD）；DisplayVersion 非空。
function Invoke-Case_02_registry_identity {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    try {
        [void](Invoke-ZzLoggUninstall $Context)
        $install = Invoke-ZzLoggInstall $Context
        if ($install.Run.TimedOut -or $install.Run.ExitCode -ne 0) {
            return (Fail-Case "前置安装失败: exit=$($install.Run.ExitCode) timeout=$($install.Run.TimedOut)")
        }

        $key = $Context.UninstallKey
        if (-not (Test-Path -Path $key)) {
            return (Fail-Case "卸载项注册表键不存在: $key")
        }
        $item = Get-ItemProperty -Path $key
        $schema = $item.UpdateIdentitySchema
        if ($null -eq $schema -or $schema -ne 2) {
            return (Fail-Case "UpdateIdentitySchema=$schema，期望 DWORD 2（引擎登记白名单对非 2 一律 Rejected）")
        }
        # Get-ItemProperty 对 DWORD 返回 Int32；确认底层类型确为 DWORD
        $valueKind = (Get-Item -Path $key).GetValueKind('UpdateIdentitySchema')
        if ($valueKind -ne [Microsoft.Win32.RegistryValueKind]::DWord) {
            return (Fail-Case "UpdateIdentitySchema 值类型为 $valueKind，期望 DWord")
        }
        if ([string]::IsNullOrWhiteSpace($item.DisplayVersion)) {
            return (Fail-Case 'DisplayVersion 为空')
        }

        return (Pass-Case "卸载项存在；UpdateIdentitySchema=2（DWORD）；DisplayVersion='$($item.DisplayVersion)'")
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
