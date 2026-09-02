param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('first-cancel', 'program', 'custom-migration', 'missing-custom', 'close-last-tab')]
  [string]$Scenario,
  [Parameter(Mandatory = $true)]
  [string]$ManualRoot,
  [Parameter(Mandatory = $true)]
  [string]$FreshRuntimeRoot,
  [switch]$ContinueScenario,
  [switch]$Wait
)

function Test-FullyQualifiedWindowsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $fullyQualifiedMethod = [System.IO.Path].GetMethod(
    'IsPathFullyQualified',
    [System.Type[]]@([string]))
  if ($null -ne $fullyQualifiedMethod) {
    return [System.IO.Path]::IsPathFullyQualified($Path)
  }

  # Windows PowerShell 5.1 runs on a .NET Framework without
  # Path.IsPathFullyQualified. Accept only drive+separator or complete UNC.
  return $Path -match '^[A-Za-z]:[\\/]' -or
    $Path -match '^\\\\[^\\/]+[\\/][^\\/]+(?:[\\/]|$)'
}

function Assert-AbsoluteNonRootPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  if (-not (Test-FullyQualifiedWindowsPath -Path $Path)) {
    throw "$Label must be fully qualified: $Path"
  }
  $fullPath = [System.IO.Path]::GetFullPath($Path)
  $pathRoot = [System.IO.Path]::GetPathRoot($fullPath).TrimEnd('\')
  if ($fullPath.TrimEnd('\') -eq $pathRoot) {
    throw "$Label must not be a filesystem root: $fullPath"
  }
  return $fullPath
}

$manualRoot = Assert-AbsoluteNonRootPath -Path $ManualRoot -Label 'Manual acceptance root'
$freshRuntimeRoot = Assert-AbsoluteNonRootPath -Path $FreshRuntimeRoot -Label 'Fresh runtime root'
$userProfile = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')
if ($manualRoot.TrimEnd('\') -eq $userProfile -or
    $manualRoot.StartsWith("$userProfile\", [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Manual acceptance root must not use the real user profile: $manualRoot"
}

$scenarioRoot = Join-Path $manualRoot "scenarios\$Scenario"
$runtimeRoot = Join-Path $scenarioRoot 'runtime'
$executable = Join-Path $runtimeRoot 'ZzLogg.exe'
$appConfigRoot = Join-Path $scenarioRoot 'smoke\app-config'
$userDataRoot = Join-Path $scenarioRoot 'smoke\user-data'
$customRoot = Join-Path $scenarioRoot 'custom'

function Assert-SafeManualPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  $fullPath = Assert-AbsoluteNonRootPath -Path $Path -Label $Label
  $fullManualRoot = $manualRoot.TrimEnd('\')

  if (-not $fullPath.StartsWith("$fullManualRoot\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "$Label escapes the prepared manual-acceptance root: $fullPath"
  }
  if ($fullPath.TrimEnd('\') -eq $userProfile -or
      $fullPath.StartsWith("$userProfile\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "$Label must not use the real user profile: $fullPath"
  }
  return $fullPath
}

$runtimeRoot = Assert-SafeManualPath -Path $runtimeRoot -Label 'Scenario runtime'
$executable = Assert-SafeManualPath -Path $executable -Label 'Scenario executable'
$appConfigRoot = Assert-SafeManualPath -Path $appConfigRoot -Label 'App-config override'
$userDataRoot = Assert-SafeManualPath -Path $userDataRoot -Label 'User-data override'
$customRoot = Assert-SafeManualPath -Path $customRoot -Label 'Custom storage root'
$freshExecutable = Join-Path $freshRuntimeRoot 'ZzLogg.exe'
if (-not (Test-Path -LiteralPath $freshExecutable -PathType Leaf)) {
  throw "The fresh Task 10 runtime is missing: $freshRuntimeRoot"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "The prepared Task 10 runtime is missing: $executable"
}
if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $freshExecutable -Algorithm SHA256).Hash) {
  throw "The scenario executable does not match the latest fresh Task 10 build: $executable"
}

$runningZzLogg = @(Get-Process -Name 'ZzLogg' -ErrorAction SilentlyContinue)
if ($runningZzLogg.Count -ne 0) {
  throw "Exit every existing ZzLogg process before changing manual scenarios. Running PIDs: $($runningZzLogg.Id -join ', ')"
}

if ($appConfigRoot -eq $userDataRoot) {
  throw 'App-config and user-data overrides must be distinct.'
}
New-Item -ItemType Directory -Path $appConfigRoot, $userDataRoot -Force | Out-Null
if (-not $ContinueScenario) {
  New-Item -ItemType Directory -Path $customRoot -Force | Out-Null
}
elseif ($Scenario -eq 'missing-custom') {
  if (Test-Path -LiteralPath $customRoot) {
    throw "The missing-custom continuation requires the selected custom root to remain absent: $customRoot"
  }
}
elseif ($Scenario -eq 'custom-migration' -and -not (Test-Path -LiteralPath $customRoot -PathType Container)) {
  throw "The custom-migration continuation lost its selected storage root: $customRoot"
}

if (-not $ContinueScenario) {
  $programLocator = Assert-SafeManualPath -Path (Join-Path $runtimeRoot 'ZzLogg.storage.ini') -Label 'Program locator'
  $programData = Assert-SafeManualPath -Path (Join-Path $runtimeRoot 'data') -Label 'Program data root'
  foreach ($target in @($programLocator, $programData)) {
    if (Test-Path -LiteralPath $target) {
      Remove-Item -LiteralPath $target -Recurse -Force
    }
  }
}

$isolatedAppData = Assert-SafeManualPath -Path (Join-Path $scenarioRoot 'environment\AppData\Roaming') -Label 'APPDATA'
$isolatedLocalAppData = Assert-SafeManualPath -Path (Join-Path $scenarioRoot 'environment\AppData\Local') -Label 'LOCALAPPDATA'
$isolatedXdgConfig = Assert-SafeManualPath -Path (Join-Path $scenarioRoot 'environment\XdgConfig') -Label 'XDG_CONFIG_HOME'
$isolatedXdgData = Assert-SafeManualPath -Path (Join-Path $scenarioRoot 'environment\XdgData') -Label 'XDG_DATA_HOME'
New-Item -ItemType Directory -Path $isolatedAppData, $isolatedLocalAppData, $isolatedXdgConfig, $isolatedXdgData -Force | Out-Null

$environmentNames = @(
  'APPDATA',
  'LOCALAPPDATA',
  'XDG_CONFIG_HOME',
  'XDG_DATA_HOME',
  'ZZLOGG_UI2_SMOKE_MS',
  'ZZLOGG_UI2_SMOKE_MODE',
  'ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR',
  'ZZLOGG_UI2_SMOKE_USER_DATA_DIR',
  'QT_QPA_PLATFORM'
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
  $savedEnvironment[$name] = @{
    Exists = Test-Path -LiteralPath "Env:$name"
    Value = [System.Environment]::GetEnvironmentVariable($name, 'Process')
  }
}

function Set-ProcessEnvironment {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [AllowNull()]
    [string]$Value
  )
  [System.Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

$process = $null
try {
  Set-ProcessEnvironment -Name 'APPDATA' -Value $isolatedAppData
  Set-ProcessEnvironment -Name 'LOCALAPPDATA' -Value $isolatedLocalAppData
  Set-ProcessEnvironment -Name 'XDG_CONFIG_HOME' -Value $isolatedXdgConfig
  Set-ProcessEnvironment -Name 'XDG_DATA_HOME' -Value $isolatedXdgData
  Set-ProcessEnvironment -Name 'ZZLOGG_UI2_SMOKE_MS' -Value '1800000'
  Set-ProcessEnvironment -Name 'ZZLOGG_UI2_SMOKE_MODE' -Value 'manual-isolation'
  Set-ProcessEnvironment -Name 'ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR' -Value $appConfigRoot
  Set-ProcessEnvironment -Name 'ZZLOGG_UI2_SMOKE_USER_DATA_DIR' -Value $userDataRoot
  Set-ProcessEnvironment -Name 'QT_QPA_PLATFORM' -Value $null

  $process = Start-Process -FilePath $executable -ArgumentList '--multi' -WorkingDirectory (Split-Path -Parent $executable) -PassThru
  Write-Output "PID=$($process.Id)"
  Write-Output "Scenario=$Scenario"
  Write-Output "ContinueScenario=$ContinueScenario"
  Write-Output "Runtime=$executable"
  Write-Output 'ZZLOGG_UI2_SMOKE_MS=1800000'
  Write-Output 'ZZLOGG_UI2_SMOKE_MODE=manual-isolation'
  Write-Output "ZZLOGG_UI2_SMOKE_APP_CONFIG_DIR=$appConfigRoot"
  Write-Output "ZZLOGG_UI2_SMOKE_USER_DATA_DIR=$userDataRoot"
  Write-Output "APPDATA=$isolatedAppData"
  Write-Output "LOCALAPPDATA=$isolatedLocalAppData"
  Write-Output "CustomRoot=$customRoot"
  Write-Output "Fixtures=$(Join-Path $scenarioRoot 'fixtures')"
}
finally {
  foreach ($name in $environmentNames) {
    $saved = $savedEnvironment[$name]
    $value = if ($saved.Exists) { $saved.Value } else { $null }
    [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
  }
}

if ($Wait) {
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) {
    throw "ZzLogg exited with code $($process.ExitCode)"
  }
}
