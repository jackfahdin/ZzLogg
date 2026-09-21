[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9]{2}\.[0-9]{2}\.[0-9]{2}$')][string]$Version,
    [ValidateSet('x64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'

# Latest stable release at migration time. Pin its official release digest so
# rebuilding a tag uses the same compiler instead of silently changing tools.
$innoVersion = '7.1.0'
$compilerDigest = '0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f'
$temporaryRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$compilerRoot = Join-Path $temporaryRoot "ZzLogg-InnoSetup-$innoVersion"
$iscc = Join-Path $compilerRoot 'ISCC.exe'
if (-not (Test-Path -LiteralPath $iscc)) {
    $download = Join-Path $temporaryRoot "innosetup-$innoVersion-x64.exe"
    Invoke-WebRequest -Uri "https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-$innoVersion-x64.exe" -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ine $compilerDigest) {
        throw 'Inno Setup compiler download SHA-256 does not match the official release'
    }
    $install = Start-Process -FilePath $download -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ALLUSERS', "/DIR=`"$compilerRoot`""
    ) -PassThru -Wait
    if ($install.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $iscc)) {
        throw "Inno Setup compiler installation failed: $($install.ExitCode)"
    }
}
# ISCC 7.1.0 intentionally has a 0.0.0.0 PE FileVersion. Query the
# supported CLI instead of rejecting the official compiler's file metadata.
$reportedVersion = (& $iscc '--version' | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $reportedVersion -cne $innoVersion) {
    throw "Unexpected Inno Setup compiler version: $reportedVersion"
}
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
& $iscc '/Qp' "/DVERSION=$Version" "/DPLATFORM=$Platform" "/O$repositoryRoot" (Join-Path $PSScriptRoot 'ZzLogg.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed: $LASTEXITCODE" }
$installer = Join-Path $repositoryRoot "ZzLogg-$Version-$Platform-Qt6-setup.exe"
if (-not (Test-Path -LiteralPath $installer) -or (Get-Item -LiteralPath $installer).Length -eq 0) {
    throw 'Inno Setup did not produce a nonempty installer'
}
