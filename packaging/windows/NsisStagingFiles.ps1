# Shared staging enumeration for the NSIS manifests: the uninstall delete set
# and the landing-manifest hash set must come from one enumeration so they can
# never drift apart. Dot-sourced by both generators; holds no state of its own.

$ErrorActionPreference = 'Stop'

# Files the packaging step itself generates into the staging root. They are
# never enumerated as payload; the uninstall manifest deletes them explicitly.
$script:ZzLoggGeneratedFileNames = @('.zzlogg-uninstall.nsh', '.zzlogg-files.manifest')

function ConvertTo-NsisPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $Path.Replace('/', '\').Replace('$', '$$')
}

function Get-ZzLoggStagedFiles {
    param([Parameter(Mandatory = $true)][string]$StagingRoot)

    $relativeFiles = @(
        Get-ChildItem -LiteralPath $StagingRoot -Recurse -File | ForEach-Object {
            $_.FullName.Substring($StagingRoot.Length).TrimStart('\', '/')
        } | Where-Object {
            $script:ZzLoggGeneratedFileNames -notcontains $_.ToLowerInvariant()
        }
    )
    [Array]::Sort($relativeFiles, [StringComparer]::OrdinalIgnoreCase)
    return $relativeFiles
}
