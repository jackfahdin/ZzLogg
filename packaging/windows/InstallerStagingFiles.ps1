# Shared enumeration of installer-owned runtime files.

$ErrorActionPreference = 'Stop'

# Files the packaging step itself generates into the staging root. They are
# never enumerated as payload. Exclude old NSIS staging leftovers as well.
$script:ZzLoggGeneratedFileNames = @('.zzlogg-uninstall.nsh', '.zzlogg-files.manifest')

function Get-ZzLoggStagedFiles {
    param([Parameter(Mandatory = $true)][string]$StagingRoot)

    $relativeFiles = @(
        Get-ChildItem -LiteralPath $StagingRoot -Recurse -File -Force | ForEach-Object {
            $_.FullName.Substring($StagingRoot.Length).TrimStart('\', '/')
        } | Where-Object {
            $script:ZzLoggGeneratedFileNames -notcontains $_.ToLowerInvariant()
        }
    )
    [Array]::Sort($relativeFiles, [StringComparer]::OrdinalIgnoreCase)
    return $relativeFiles
}
