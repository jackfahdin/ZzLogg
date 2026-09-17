# Generates the landing manifest .zzlogg-files.manifest: one entry per
# installer-owned staged file with its size and SHA-256, so the transaction
# engine can diff installations without scanning directories. Enumerates the
# same staging tree as the uninstall manifest (shared NsisStagingFiles.ps1).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$StagingDirectory
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NsisStagingFiles.ps1')

$stagingRoot = (Resolve-Path -LiteralPath $StagingDirectory).Path.TrimEnd('\', '/')
$manifestPath = Join-Path $stagingRoot '.zzlogg-files.manifest'
if (Test-Path -LiteralPath $manifestPath) {
    Remove-Item -LiteralPath $manifestPath -Force
}

# Binary layout shared with the transaction engine (txengine_win.cpp):
# "ZZTXMAN1", u32le version (1), u32le entry count, then per entry
# u32le utf8 path byte count, u64le file size, 32 raw SHA-256 bytes, utf8 path
# with '/' separators. Bounds: 4096 entries, 384 path bytes, 4 MiB total.
$maxEntries = 4096
$maxPathBytes = 384
$maxManifestBytes = 4 * 1024 * 1024

$relativeFiles = Get-ZzLoggStagedFiles -StagingRoot $stagingRoot
if ($relativeFiles.Count -gt $maxEntries) {
    throw "Staging tree holds $($relativeFiles.Count) files, above the $maxEntries entry manifest bound"
}

# ASCII-only case fold, mirroring the engine's duplicate key.
function ConvertTo-ManifestKey {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [regex]::Replace($Path, '[a-z]', { param($m) $m.Value.ToUpperInvariant() })
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('ZZTXMAN1'))
    $writer.Write([uint32]1)
    $writer.Write([uint32]$relativeFiles.Count)

    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($relativeFile in $relativeFiles) {
        $normalized = $relativeFile.Replace('\', '/')
        $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($normalized)
        if ($pathBytes.Length -lt 1 -or $pathBytes.Length -gt $maxPathBytes) {
            throw "Manifest path violates the $maxPathBytes byte bound: $normalized"
        }
        if ($normalized.StartsWith('/') -or $normalized.EndsWith('/')) {
            throw "Manifest path must be relative without edge separators: $normalized"
        }
        foreach ($component in $normalized.Split('/')) {
            if ($component -eq '' -or $component -eq '.' -or $component -eq '..' `
                    -or $component.EndsWith('.') -or $component.EndsWith(' ')) {
                throw "Manifest path has an unsafe component '$component' in: $normalized"
            }
            if ($component -match '[\\:\x00-\x1f]') {
                throw "Manifest path component '$component' contains a forbidden character"
            }
        }
        if (-not $seen.Add((ConvertTo-ManifestKey $normalized))) {
            throw "Manifest paths collide under ASCII case folding: $normalized"
        }

        $fullPath = Join-Path $stagingRoot $relativeFile
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        $fileStream = [IO.File]::OpenRead($fullPath)
        try {
            # Size comes from the already open stream: no second stat between
            # hashing and sizing, so both describe the same opened file.
            $size = [uint64]$fileStream.Length
            $hashBytes = $sha256.ComputeHash($fileStream)
        }
        finally {
            $fileStream.Dispose()
            $sha256.Dispose()
        }

        $writer.Write([uint32]$pathBytes.Length)
        $writer.Write([uint64]$size)
        $writer.Write($hashBytes)
        $writer.Write($pathBytes)
    }
    $writer.Flush()
    $bytes = $stream.ToArray()
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

if ($bytes.Length -gt $maxManifestBytes) {
    throw "Landing manifest exceeds the $maxManifestBytes byte bound: $($bytes.Length)"
}
[IO.File]::WriteAllBytes($manifestPath, $bytes)

Write-Output "Generated landing manifest for $($relativeFiles.Count) staged files: $manifestPath"
