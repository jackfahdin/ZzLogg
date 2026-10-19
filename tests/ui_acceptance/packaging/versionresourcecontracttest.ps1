param(
  [Parameter(Mandatory = $true)] [string]$MainApp,
  [Parameter(Mandatory = $true)] [string]$GrepApp
)

$expectedOriginalFilenames = @{
  $MainApp = 'ZzLogg.exe'
  $GrepApp = 'ZzLogg_grep.exe'
}

foreach ($app in $expectedOriginalFilenames.Keys) {
  if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "Expected application does not exist: $app"
  }

  $version = (Get-Item -LiteralPath $app).VersionInfo
  $expectedText = '{0:D2}.{1:D2}.{2:D2}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart
  if ($version.FileVersion -cne $expectedText -or $version.ProductVersion -cne $expectedText) {
    throw "Expected padded version $expectedText for $app, got $($version.FileVersion) / $($version.ProductVersion)"
  }
  if ($version.OriginalFilename -ne $expectedOriginalFilenames[$app]) {
    throw "Expected $($expectedOriginalFilenames[$app]) as OriginalFilename for $app, got $($version.OriginalFilename)"
  }
  if ($version.ProductName -ne 'ZzLogg') {
    throw "Expected ZzLogg as ProductName for $app, got $($version.ProductName)"
  }
  if ($version.FileDescription -ne 'ZzLogg log viewer') {
    throw "Expected ZzLogg log viewer as FileDescription for $app, got $($version.FileDescription)"
  }
  if ($version.CompanyName -ne 'Jackfahdin') {
    throw "Expected Jackfahdin as CompanyName for $app, got $($version.CompanyName)"
  }
}
