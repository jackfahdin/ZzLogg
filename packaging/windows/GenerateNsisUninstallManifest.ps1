[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$StagingDirectory
)

$ErrorActionPreference = 'Stop'

$stagingRoot = (Resolve-Path -LiteralPath $StagingDirectory).Path.TrimEnd('\', '/')
$manifestPath = Join-Path $stagingRoot '.zzlogg-uninstall.nsh'
if (Test-Path -LiteralPath $manifestPath) {
    Remove-Item -LiteralPath $manifestPath -Force
}

function ConvertTo-NsisPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $Path.Replace('/', '\').Replace('$', '$$')
}

$relativeFiles = @(
    Get-ChildItem -LiteralPath $stagingRoot -Recurse -File | ForEach-Object {
        $_.FullName.Substring($stagingRoot.Length).TrimStart('\', '/')
    }
)
[Array]::Sort($relativeFiles, [StringComparer]::OrdinalIgnoreCase)

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
