param([ValidateSet('x64')][string]$Arch = 'x64')
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) {
    throw 'Visual Studio with MSVC x64 tools was not found'
}
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments "-arch=$Arch -host_arch=x64"
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Visual Studio developer shell did not expose cl.exe'
}
# Persist only variables changed by Visual Studio, including PATH, INCLUDE and
# LIB. The next composite action runs its build commands in a separate shell.
Get-ChildItem Env: | Where-Object { $before[$_.Name] -cne $_.Value } | ForEach-Object {
    if ($_.Value -match "[\r\n]") { throw "Unexpected multiline environment variable: $($_.Name)" }
    "$($_.Name)=$($_.Value)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}
