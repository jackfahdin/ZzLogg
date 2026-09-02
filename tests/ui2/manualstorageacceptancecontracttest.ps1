param(
  [Parameter(Mandatory = $true)]
  [string]$Launcher
)

foreach ($unsafeRoot in @('D:relative', '\relative')) {
  try {
    & $Launcher `
      -Scenario first-cancel `
      -ManualRoot $unsafeRoot `
      -FreshRuntimeRoot 'D:\fresh-runtime'
    throw "Launcher accepted a drive/root-relative path: $unsafeRoot"
  }
  catch {
    if ($_.Exception.Message -notmatch 'fully qualified') {
      throw "Launcher did not reject '$unsafeRoot' as non-fully-qualified: $($_.Exception.Message)"
    }
  }
}

Write-Output 'Manual storage acceptance path rejection contract passed.'
