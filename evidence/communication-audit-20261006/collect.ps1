param(
  [string]$LogPath = 'D:\data\download\esp32-local-history-2026-10-06T08-01-00-423Z.json',
  [string]$BootId = '4459f5864ed3b973'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$history = Get-Content -LiteralPath $LogPath -Raw | ConvertFrom-Json
$rows = @($history.logs | Where-Object { $_.bootId -eq $BootId })
if (!$rows.Count) { throw "No records for boot $BootId" }
$counts = [ordered]@{}
foreach ($name in @('CAT_SEARCH_FAILED','LINK_RECOVERY','MOTION_FAILED','STOP_UNCONFIRMED','TELEMETRY_TIMEOUT','UART_BAD_FRAME','UNO_PROTOCOL_ERROR','CRC_ERROR','CRC_MISSING','TELEMETRY_INCOMPLETE')) {
  $counts[$name] = @($rows | Where-Object { $_.source -eq $name }).Count
}
$summary = [ordered]@{
  source = $LogPath
  sha256 = (Get-FileHash -LiteralPath $LogPath -Algorithm SHA256).Hash
  exportedAt = $history.exportedAt
  bootId = $BootId
  lastStateFirmware = $history.lastState.firmware
  recordCount = $rows.Count
  uniqueKeys = @($rows.key | Sort-Object -Unique).Count
  counting = 'Records in selected boot only; not a long-run failure rate'
  counts = $counts
  sourceCommits = [ordered]@{
    maintenance = '9dfc6b284a05755d18802ed4da1584e820c15c14'
    main042 = 'c45e66a5edafbd503e180d2a0326eb84567521d9'
  }
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'summary.json') -Encoding UTF8
# Include the diagnostic heartbeats as well as decisions and commands.
@($rows | Where-Object { $_.ms -ge 370340 -and $_.ms -le 377030 }) |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'incident.json') -Encoding UTF8
$inventory = [System.Collections.Generic.List[string]]::new()
foreach ($revision in @($summary.sourceCommits.maintenance, $summary.sourceCommits.main042)) {
  foreach ($file in @('esp32-host/ESP32CatHost/policy.cpp','esp32-host/ESP32CatHost/ESP32CatHost.ino','esp32-host/ESP32CatHost/uno_debug.h','firmware/UnoCatController/scan_machine.h')) {
    $source = @(git -C $repo show "${revision}:$file")
    if ($LASTEXITCODE -ne 0) { throw "git show failed: $revision $file" }
    for ($i=0; $i -lt $source.Count; $i++) {
      if ($source[$i] -match 'catFailed|exitInteraction|finish\(|fail\(|recoverLink|retryLink|retryScan|TIMEOUT|UNCONFIRMED|CRC_MISSING|CRC_ERROR|TELEMETRY_|BAD_CHECKSUM|TX_OVERFLOW|STALE_VISION|SETTLE_TIMEOUT') {
        $inventory.Add("${revision}:${file}:$($i+1): $($source[$i])")
      }
    }
  }
}
$inventory | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'source-exits.txt') -Encoding UTF8
Write-Output ($summary | ConvertTo-Json -Depth 6)
