# 用例 01 install-silent（CI 可跑）
# PASS 判据：`setup.exe /VERYSILENT` 退出 0；`$InstallDir\ZzLogg.exe`、`.zzlogg-install-root`、
# `.zzlogg-files.manifest` 存在；manifest 首行 schema=2。
# 协议映射说明：落地清单是二进制格式（GenerateInstallerManifest.ps1 与 txengine_win.cpp
# 同源：magic "ZZTXMAN1" + u32le version(1) + u32le count），没有文本"首行"；
# "schema 2" 落地身份由注册表 UpdateIdentitySchema=2 承载（用例 02 断言）。
# 此处断言二进制头部 magic=="ZZTXMAN1" 且 version==1，即引擎/探针认可的清单形状。
function Invoke-Case_01_install_silent {
    param($Context)

    $prereq = Test-CasePrereqs $Context
    if ($prereq) { return $prereq }

    try {
        # 幂等预清理：已有安装先卸载
        [void](Invoke-ZzLoggUninstall $Context)

        $install = Invoke-ZzLoggInstall $Context
        $target = $install.TargetDir
        if ($install.Run.TimedOut) {
            return (Fail-Case "setup.exe /VERYSILENT 超时（$($target)），进程被终止")
        }
        if ($install.Run.ExitCode -ne 0) {
            return (Fail-Case "setup.exe /VERYSILENT 退出码 $($install.Run.ExitCode)，期望 0")
        }

        $exe = Join-Path $target 'ZzLogg.exe'
        $marker = Join-Path $target '.zzlogg-install-root'
        $manifest = Join-Path $target '.zzlogg-files.manifest'
        $missing = @()
        foreach ($path in @($exe, $marker, $manifest)) {
            if (-not (Test-Path -LiteralPath $path)) { $missing += $path }
        }
        if ($missing) {
            return (Fail-Case ("安装后缺少文件: " + ($missing -join ', ')))
        }

        $bytes = [IO.File]::ReadAllBytes($manifest)
        if ($bytes.Length -lt 16) {
            return (Fail-Case ".zzlogg-files.manifest 长度 $($bytes.Length) 小于 16 字节头部")
        }
        $magic = [Text.Encoding]::ASCII.GetString($bytes[0..7])
        $version = [BitConverter]::ToUInt32($bytes, 8)
        if ($magic -ne 'ZZTXMAN1' -or $version -ne 1) {
            return (Fail-Case "清单头部不符: magic='$magic' version=$version（期望 ZZTXMAN1/1，即 schema 2 落地清单的二进制形状）")
        }

        return (Pass-Case "setup.exe /VERYSILENT 退出 0；ZzLogg.exe/.zzlogg-install-root/.zzlogg-files.manifest 均存在；manifest magic=ZZTXMAN1 version=1（schema 2 落地清单，schema 身份由 UpdateIdentitySchema=2 承载，见用例 02）")
    }
    finally {
        [void](Invoke-ZzLoggUninstall $Context)
        Remove-ZzLoggDirGuarded $Context.InstallDir
    }
}
