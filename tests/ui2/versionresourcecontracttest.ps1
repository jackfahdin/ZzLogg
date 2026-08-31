param(
  [Parameter(Mandatory = $true)] [string]$MainApp,
  [Parameter(Mandatory = $true)] [string]$PortableApp,
  [Parameter(Mandatory = $true)] [string]$GrepApp,
  [Parameter(Mandatory = $true)] [string]$Ui2App
)

$expectedOriginalFilenames = @{
  $MainApp = 'ZzLogg.exe'
  $PortableApp = 'ZzLogg_portable.exe'
  $GrepApp = 'ZzLogg_grep.exe'
  $Ui2App = 'ZzLogg_ui2.exe'
}

foreach ($app in $expectedOriginalFilenames.Keys) {
  if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "Expected application does not exist: $app"
  }

  $version = (Get-Item -LiteralPath $app).VersionInfo
  if ($version.OriginalFilename -ne $expectedOriginalFilenames[$app]) {
    throw "Expected $($expectedOriginalFilenames[$app]) as OriginalFilename for $app, got $($version.OriginalFilename)"
  }
  if ($version.ProductName -ne 'ZzLogg') {
    throw "Expected ZzLogg as ProductName for $app, got $($version.ProductName)"
  }
  if ($version.FileDescription -ne 'ZzLogg log viewer') {
    throw "Expected ZzLogg log viewer as FileDescription for $app, got $($version.FileDescription)"
  }
  if ($version.CompanyName -ne 'JackfahdinQt') {
    throw "Expected JackfahdinQt as CompanyName for $app, got $($version.CompanyName)"
  }
}
