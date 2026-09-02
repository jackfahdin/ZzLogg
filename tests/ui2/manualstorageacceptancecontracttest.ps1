param(
  [Parameter(Mandatory = $true)]
  [string]$Launcher,
  [Parameter(Mandatory = $true)]
  [string]$TestRoot
)

function Assert-LauncherRejects {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ManualRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedMessage,
    [Parameter(Mandatory = $true)]
    [string]$Description
  )

  try {
    & $Launcher `
      -Scenario first-cancel `
      -ManualRoot $ManualRoot `
      -FreshRuntimeRoot 'D:\fresh-runtime'
    throw "Launcher accepted $Description`: $ManualRoot"
  }
  catch {
    if ($_.Exception.Message -notmatch $ExpectedMessage) {
      throw "Launcher did not reject $Description safely: $($_.Exception.Message)"
    }
  }
}

foreach ($unsafeRoot in @('D:relative', '\relative')) {
  Assert-LauncherRejects `
    -ManualRoot $unsafeRoot `
    -ExpectedMessage 'fully qualified' `
    -Description 'a drive/root-relative path'
}

$profileDrive = [System.IO.Path]::GetPathRoot($env:USERPROFILE).TrimEnd('\')
foreach ($deviceRoot in @(
    "\\?\$env:USERPROFILE\zzlogg-manual-contract-probe",
    "\\.\$profileDrive\zzlogg-manual-contract-probe")) {
  Assert-LauncherRejects `
    -ManualRoot $deviceRoot `
    -ExpectedMessage 'device namespace' `
    -Description 'a Windows device-namespace path'
}

$resolvedTestRoot = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $resolvedTestRoot) {
  Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedTestRoot -Force | Out-Null
try {
  $junctionTarget = Join-Path $resolvedTestRoot 'junction-target'
  $junctionPath = Join-Path $resolvedTestRoot 'junction-root'
  New-Item -ItemType Directory -Path $junctionTarget -Force | Out-Null
  New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null

  Assert-LauncherRejects `
    -ManualRoot $junctionPath `
    -ExpectedMessage 'reparse point' `
    -Description 'a reparse-point-backed manual root'
}
finally {
  if (Test-Path -LiteralPath $resolvedTestRoot) {
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
  }
}

Write-Output 'Manual storage acceptance path rejection contract passed.'
