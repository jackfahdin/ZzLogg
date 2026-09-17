[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$StagingDirectory
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NsisStagingFiles.ps1')

$stagingRoot = (Resolve-Path -LiteralPath $StagingDirectory).Path.TrimEnd('\', '/')
$manifestPath = Join-Path $stagingRoot '.zzlogg-uninstall.nsh'
if (Test-Path -LiteralPath $manifestPath) {
    Remove-Item -LiteralPath $manifestPath -Force
}

$relativeFiles = Get-ZzLoggStagedFiles -StagingRoot $stagingRoot

$directories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($relativeFile in $relativeFiles) {
    $directory = [IO.Path]::GetDirectoryName($relativeFile)
    while (-not [string]::IsNullOrEmpty($directory)) {
        [void]$directories.Add($directory)
        $directory = [IO.Path]::GetDirectoryName($directory)
    }
}

$sortedDirectories = @(
    $directories | Sort-Object `
        @{ Expression = { ($_ -split '[\\/]').Count }; Descending = $true }, `
        @{ Expression = { $_ }; Descending = $false }
)

$lines = New-Object 'System.Collections.Generic.List[string]'
$lines.Add('; Generated from the complete Windows installer staging tree. Do not edit.')
foreach ($relativeFile in $relativeFiles) {
    $lines.Add(('Delete "$INSTDIR\{0}"' -f (ConvertTo-NsisPath $relativeFile)))
}
$lines.Add('Delete "$INSTDIR\Uninstall.exe"')
$lines.Add('Delete "$INSTDIR\.zzlogg-install-root"')
$lines.Add('Delete "$INSTDIR\.zzlogg-files.manifest"')
foreach ($directory in $sortedDirectories) {
    $lines.Add(('RMDir "$INSTDIR\{0}"' -f (ConvertTo-NsisPath $directory)))
}
$lines.Add('RMDir "$INSTDIR"')

$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText(
    $manifestPath,
    (($lines -join "`r`n") + "`r`n"),
    $utf8WithoutBom)

Write-Output "Generated NSIS uninstall manifest for $($relativeFiles.Count) staged files: $manifestPath"
