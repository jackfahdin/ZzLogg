param(
    [Parameter(Mandatory)][string]$BaselineExe,
    [Parameter(Mandatory)][string]$CurrentExe,
    [Parameter(Mandatory)][string]$QtBin,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1, 20)][int]$Runs = 5
)
$ErrorActionPreference = 'Stop'
$BaselineExe = (Resolve-Path -LiteralPath $BaselineExe).Path
$CurrentExe = (Resolve-Path -LiteralPath $CurrentExe).Path
$QtBin = (Resolve-Path -LiteralPath $QtBin).Path
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'OutputDirectory must be new so earlier measurements are not overwritten.'
}
$null = New-Item -ItemType Directory -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
[ordered]@{
    RecordedAt = (Get-Date).ToString('o')
    BaselineExe = $BaselineExe
    BaselineSha256 = (Get-FileHash -LiteralPath $BaselineExe -Algorithm SHA256).Hash
    CurrentExe = $CurrentExe
    CurrentSha256 = (Get-FileHash -LiteralPath $CurrentExe -Algorithm SHA256).Hash
    QtBin = $QtBin
    Runs = $Runs
    ScrollSettleMs = $env:ZZLOGG_SCROLL_SETTLE_MS
    ScrollSteps = $env:ZZLOGG_SCROLL_STEPS
    ScrollMotion = $env:ZZLOGG_SCROLL_MOTION
    Note = 'Warm file cache; QTRY timings include approximately 50 ms polling granularity; process peak includes fixture generation.'
} | ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $OutputDirectory 'manifest.json')
$previousPath = $env:PATH
$previousPlugins = $env:QT_PLUGIN_PATH
$previousTimeout = $env:QTEST_FUNCTION_TIMEOUT
$rows = [System.Collections.Generic.List[object]]::new()
try {
    $env:PATH = "$QtBin;$previousPath"
    $env:QT_PLUGIN_PATH = Join-Path (Split-Path $QtBin) 'plugins'
    $env:QTEST_FUNCTION_TIMEOUT = '120000'
    # Round zero is warm-up; alternate order to reduce systematic order bias.
    for ($round = 0; $round -le $Runs; ++$round) {
        $order = if ($round % 2 -eq 0) { @('baseline', 'current') } else { @('current', 'baseline') }
        foreach ($version in $order) {
            $exe = if ($version -eq 'baseline') { $BaselineExe } else { $CurrentExe }
            $report = Join-Path $OutputDirectory "$version-$round.txt"
            # Run the interactive test normally: a hidden launch cannot measure repaint.
            # QTEST_FUNCTION_TIMEOUT bounds the test case; qWaitForWindowExposed verifies exposure.
            & $exe opensAndSearchesLargeLog -platform windows -o "$report,txt"
            if ($LASTEXITCODE -ne 0) {
                throw "Benchmark failed: $version round $round; see $report"
            }
            $log = Get-Content -LiteralPath $report -Raw
            $timings = [regex]::Match($log, 'bytes=(\d+), lines=2097152, open_ms=(\d+), search_ms=(\d+), scroll_ms=(\d+)')
            $memory = [regex]::Match($log, 'peak_working_set_bytes=(\d+)')
            $segments = [regex]::Match($log, 'set_value_ns=(\d+), repaint_ns=(\d+), events_ns=(\d+)')
            if (-not $timings.Success -or -not $memory.Success -or $log -notmatch '0 failed') {
                throw "Missing benchmark evidence: $report"
            }
            $row = [pscustomobject]@{
                Version = $version; Round = $round; Warmup = ($round -eq 0)
                Bytes = [long]$timings.Groups[1].Value
                OpenMs = [long]$timings.Groups[2].Value
                SearchMs = [long]$timings.Groups[3].Value
                ScrollMs = [long]$timings.Groups[4].Value
                PeakWorkingSetBytes = [long]$memory.Groups[1].Value
                SetValueNs = if ($segments.Success) { [long]$segments.Groups[1].Value } else { $null }
                RepaintNs = if ($segments.Success) { [long]$segments.Groups[2].Value } else { $null }
                EventsNs = if ($segments.Success) { [long]$segments.Groups[3].Value } else { $null }
            }
            $rows.Add($row)
            $rows | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $OutputDirectory 'results.csv')
            $row | Format-Table
        }
    }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPlugins
    $env:QTEST_FUNCTION_TIMEOUT = $previousTimeout
}
