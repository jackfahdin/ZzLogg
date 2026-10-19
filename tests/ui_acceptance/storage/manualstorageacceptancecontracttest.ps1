param(
  [Parameter(Mandatory = $true)]
  [string]$Launcher,
  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,
  [Parameter(Mandatory = $true)]
  [string]$AllowedRoot
)

function Test-WindowsDeviceNamespacePath {
  param([Parameter(Mandatory = $true)][string]$Path)

  $normalizedPath = $Path.Replace('/', '\')
  foreach ($prefix in @('\\?\', '\\.\', '\??\', '\\??\')) {
    if ($normalizedPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
      return $true
    }
  }
  return $false
}

function Assert-NoReparsePointInExistingPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Label
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $pathRoot = [System.IO.Path]::GetPathRoot($fullPath).TrimEnd('\')
  $current = $fullPath.TrimEnd('\')
  while ($true) {
    if (Test-Path -LiteralPath $current) {
      $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
      if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not traverse a reparse point: $current"
      }
    }
    if ($current -eq $pathRoot) {
      break
    }
    $current = [System.IO.Path]::GetDirectoryName($current).TrimEnd('\')
  }
}

function Resolve-AbsoluteNonRootPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Label
  )

  if (Test-WindowsDeviceNamespacePath -Path $Path) {
    throw "$Label must not use a Windows device namespace: $Path"
  }
  $fullPath = [System.IO.Path]::GetFullPath($Path)
  if (-not [System.IO.Path]::IsPathRooted($fullPath) -or
      $fullPath.TrimEnd('\') -eq [System.IO.Path]::GetPathRoot($fullPath).TrimEnd('\')) {
    throw "$Label must be an absolute non-root path: $Path"
  }
  return $fullPath.TrimEnd('\')
}

function Assert-LauncherRejects {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ManualRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedMessage,
    [Parameter(Mandatory = $true)]
    [string]$Description,
    [ValidateSet('first-cancel', 'program')]
    [string]$Scenario = 'first-cancel',
    [switch]$ContinueScenario
  )

  try {
    $parameters = @{
      Scenario = $Scenario
      ManualRoot = $ManualRoot
      FreshRuntimeRoot = 'D:\fresh-runtime'
    }
    if ($ContinueScenario) {
      $parameters.ContinueScenario = $true
    }
    & $Launcher @parameters
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

$resolvedRepositoryRoot = Resolve-AbsoluteNonRootPath -Path $RepositoryRoot -Label 'Repository root'
$resolvedAllowedRoot = Resolve-AbsoluteNonRootPath -Path $AllowedRoot -Label 'Allowed contract root'
if ((Split-Path -Leaf $resolvedAllowedRoot) -ne 'manual-storage-acceptance-contracts' -or
    -not $resolvedAllowedRoot.StartsWith(
      "$resolvedRepositoryRoot\",
      [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Allowed contract root must be the dedicated repository test directory: $resolvedAllowedRoot"
}
Assert-NoReparsePointInExistingPath -Path $resolvedAllowedRoot -Label 'Allowed contract root'
New-Item -ItemType Directory -Path $resolvedAllowedRoot -Force | Out-Null
Assert-NoReparsePointInExistingPath -Path $resolvedAllowedRoot -Label 'Allowed contract root'

$resolvedTestRoot = Join-Path $resolvedAllowedRoot ([Guid]::NewGuid().ToString('N'))
if (([System.IO.Path]::GetDirectoryName($resolvedTestRoot)).TrimEnd('\') -ne $resolvedAllowedRoot) {
  throw "Generated contract root escaped its allowed parent: $resolvedTestRoot"
}
New-Item -ItemType Directory -Path $resolvedTestRoot | Out-Null
$manualRootJunction = $null
$programDataJunction = $null
try {
  $junctionTarget = Join-Path $resolvedTestRoot 'junction-target'
  $manualRootJunction = Join-Path $resolvedTestRoot 'junction-root'
  New-Item -ItemType Directory -Path $junctionTarget -Force | Out-Null
  New-Item -ItemType Junction -Path $manualRootJunction -Target $junctionTarget | Out-Null

  Assert-LauncherRejects `
    -ManualRoot $manualRootJunction `
    -ExpectedMessage 'reparse point' `
    -Description 'a reparse-point-backed manual root'

  $continuationRoot = Join-Path $resolvedTestRoot 'continuation'
  $runtimeRoot = Join-Path $continuationRoot 'scenarios\program\runtime'
  $programDataTarget = Join-Path $resolvedTestRoot 'outside-program-data'
  $programDataJunction = Join-Path $runtimeRoot 'data'
  New-Item -ItemType Directory -Path $runtimeRoot, $programDataTarget -Force | Out-Null
  New-Item -ItemType Junction -Path $programDataJunction -Target $programDataTarget | Out-Null

  Assert-LauncherRejects `
    -ManualRoot $continuationRoot `
    -ExpectedMessage 'reparse point' `
    -Description 'a continuation whose program data traverses a reparse point' `
    -Scenario program `
    -ContinueScenario
}
finally {
  foreach ($junction in @($programDataJunction, $manualRootJunction)) {
    if ($null -ne $junction -and (Test-Path -LiteralPath $junction)) {
      Remove-Item -LiteralPath $junction -Force
    }
  }
  if (Test-Path -LiteralPath $resolvedTestRoot) {
    Assert-NoReparsePointInExistingPath -Path $resolvedTestRoot -Label 'Generated contract root'
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
  }
}

Write-Output 'Manual storage acceptance path rejection contract passed.'
