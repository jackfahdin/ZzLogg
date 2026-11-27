# Observe a real, user-triggered update. Run in a disposable Windows VM with
# two locally built packages; this does not fabricate coordinator credentials.
#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Application,
    [Parameter(Mandatory=$true)][int]$OriginalProcessId,
    [Parameter(Mandatory=$true)][string]$ExpectedVersion,
    [Parameter(Mandatory=$true)][string]$DataDirectory,
    [Parameter(Mandatory=$true)][string]$ReportPath,
    [ValidateRange(1,3600)][int]$TimeoutSeconds=900
)
$ErrorActionPreference='Stop'
$report=@{ status='FAIL'; oldPid=$OriginalProcessId; expectedVersion=$ExpectedVersion }
$sentinel=$null
try {
    $applicationPath=(Resolve-Path -LiteralPath $Application).Path
    $original=Get-Process -Id $OriginalProcessId -ErrorAction Stop
    if ($original.Path -ine $applicationPath) { throw 'Original PID does not belong to the supplied application.' }
    $null=$original.Handle
    $beforeVersion=(Get-Item -LiteralPath $applicationPath).VersionInfo.ProductVersion
    if ($beforeVersion -eq $ExpectedVersion) { throw 'The base application already has the target version; install the older test package first.' }
    $beforeHash=(Get-FileHash -LiteralPath $applicationPath -Algorithm SHA256).Hash
    if (-not (Test-Path -LiteralPath $DataDirectory -PathType Container)) { throw 'Isolated test data directory does not exist.' }
    $sentinel=Join-Path $DataDirectory ('update-acceptance-'+[guid]::NewGuid().ToString('N')+'.txt')
    $sentinelContent=[guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($sentinel,$sentinelContent)
    $updateLog=Join-Path $DataDirectory 'logs\update-last.log'
    $previousLogTime=if(Test-Path -LiteralPath $updateLog) { (Get-Item -LiteralPath $updateLog).LastWriteTimeUtc } else { [DateTime]::MinValue }
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    Write-Host '请在已打开的 ZzLogg 中执行检查更新、下载、安装，并确认 UAC。此窗口会验证旧进程退出及新版本窗口响应。'
    $candidate=$null
    while([DateTime]::UtcNow -lt $deadline) {
        if(Test-Path -LiteralPath $updateLog) {
            $logFile=Get-Item -LiteralPath $updateLog
            if($logFile.LastWriteTimeUtc -gt $previousLogTime) {
                # The coordinator still holds a writable handle: sharing must
                # allow the existing writer as well as concurrent readers.
                $stream=[IO.File]::Open($updateLog,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
                $reader=New-Object IO.StreamReader($stream)
                try { $logText=$reader.ReadToEnd() } finally { $reader.Dispose();$stream.Dispose() }
                if($logText -match 'result=(failed|manual_restart_required|installed_restart_failed)') {
                    throw "Updater reported failure or unconfirmed restart. See $updateLog`n$logText"
                }
                $original.Refresh()
                if($original.HasExited -and $logText -match 'result=complete') {
                    $candidate=Get-Process -Name 'ZzLogg' -ErrorAction SilentlyContinue |
                        Where-Object { $_.Id -ne $OriginalProcessId -and $_.Path -ieq $applicationPath -and $_.MainWindowHandle -ne 0 -and $_.Responding } |
                        Select-Object -First 1
                    if($candidate) { break }
                }
            }
        }
        Start-Sleep -Milliseconds 250
    }
    if(-not $candidate) { throw "Timed out waiting for a responding new application. See $updateLog" }
    $null=$candidate.Handle
    # Retain the process object (PID reuse cannot stand in for this candidate).
    Start-Sleep -Seconds 2
    $candidate.Refresh()
    if($candidate.HasExited -or -not $candidate.Responding -or $candidate.MainWindowHandle -eq 0) { throw 'The restarted application exited or stopped responding.' }
    $afterVersion=(Get-Item -LiteralPath $applicationPath).VersionInfo.ProductVersion
    $afterHash=(Get-FileHash -LiteralPath $applicationPath -Algorithm SHA256).Hash
    if($afterVersion -ne $ExpectedVersion -or $afterHash -eq $beforeHash) { throw "Application bytes/version did not advance: $beforeVersion -> $afterVersion" }
    if(-not (Test-Path -LiteralPath $sentinel) -or [IO.File]::ReadAllText($sentinel) -ne $sentinelContent) { throw 'Update modified or removed user data.' }
    $report.status='PASS';$report.newPid=$candidate.Id;$report.version=$afterVersion
    $report.applicationSha256=$afterHash;$report.dataPreserved=$true;$report.updateLog=$updateLog
}
catch { $report.error=$_.Exception.Message;Write-Host $report.error }
finally {
    if($sentinel -and (Test-Path -LiteralPath $sentinel)) { Remove-Item -LiteralPath $sentinel }
    $reportDirectory=Split-Path -Parent ([IO.Path]::GetFullPath($ReportPath))
    [IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
    $report | ConvertTo-Json | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}
if($report.status -ne 'PASS') { exit 1 }
Write-Host "PASS: $ReportPath"
exit 0
