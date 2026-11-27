# A live base-version application creates the real production credentials.
# The operator triggers the update; the verifier checks actual process/window,
# binary version, result log and preservation of unrelated user data.
function Invoke-Case_08_upgrade_endtoend {
    param($Context)
    if(-not $Context.UpdateOriginalProcessId) {
        return (Skip-Case '需要已启动的旧版应用：传入 -UpdateOriginalProcessId、-UpdateExpectedVersion、-UpdateDataDirectory；也可使用 windows_local_update.py serve --verify')
    }
    if(-not $Context.UpdateExpectedVersion -or -not $Context.UpdateDataDirectory) {
        return (Fail-Case '必须同时指定目标版本和隔离测试数据目录')
    }
    $report=Join-Path $Context.UpdateDataDirectory 'update-acceptance.json'
    $verifier=Join-Path $PSScriptRoot '..\Verify-UpdateRestart.ps1'
    & (Get-Process -Id $PID).Path -NoProfile -ExecutionPolicy Bypass -File $verifier `
        -Application (Join-Path $Context.InstallDir 'ZzLogg.exe') `
        -OriginalProcessId $Context.UpdateOriginalProcessId -ExpectedVersion $Context.UpdateExpectedVersion `
        -DataDirectory $Context.UpdateDataDirectory -ReportPath $report | Out-Host
    if($LASTEXITCODE -ne 0) { return (Fail-Case "真实升级未通过，报告：$report") }
    return (Pass-Case "旧进程退出、新版本窗口响应、文件版本变化、用户数据保留；报告：$report")
}
