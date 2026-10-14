param([string]$OldPath = 'D:/data/download/esp32-local-history-2026-10-13T15-41-52-587Z.json')
$ErrorActionPreference = 'Stop'
function Stats($values) {
  $v = @($values | Sort-Object); if (!$v.Count) { return $null }
  $mid = [int][Math]::Floor($v.Count/2)
  $median = if ($v.Count % 2) { $v[$mid] } else { ($v[$mid-1]+$v[$mid])/2 }
  return @{n=$v.Count; min=$v[0]; median=$median; p95=$v[[int][Math]::Ceiling(.95*$v.Count)-1]; max=$v[-1]}
}
function Analyze($events) {
  $events = @($events | Where-Object {$null -ne $_.ms} | Sort-Object ms,seq)
  $tx = @($events | Where-Object {$_.source -eq 'UNO_TX' -and $_.line -match '^C \d+ GET_TELEMETRY'})
  $done=@(); $ack=@(); $interval=@(); $completeIds=@()
  foreach ($t in $tx) {
    $id=($t.line -split ' ')[1]
    $rx=@($events | Where-Object {$_.source -eq 'UNO_RX' -and $_.ms -ge $t.ms -and $_.line -match "^(AK|TB|DONE) $id( |$)"})
    $d=@($rx | Where-Object {$_.line -match "^DONE $id TELEMETRY"})
    $parts=@($rx | Where-Object {$_.line -match '^TB '} | ForEach-Object {($_.line -split ' ')[2]} | Sort-Object -Unique)
    $a=@($rx | Where-Object {$_.line -match '^AK '})
    if ($a.Count) {$ack += $a[0].ms-$t.ms}
    if ($d.Count -and $parts.Count -eq 3) {
      $done += $d[0].ms-$t.ms; $completeIds += $id
    }
  }
  for($i=1;$i -lt $tx.Count;$i++) {$interval += $tx[$i].ms-$tx[$i-1].ms}
  $grants=@($events | Where-Object {$_.source -eq 'UART_AVAILABLE'}); $grantGaps=@()
  for($i=1;$i -lt $grants.Count;$i++) {$grantGaps += $grants[$i].ms-$grants[$i-1].ms}
  $stops=@(); foreach($t in $events | Where-Object {$_.source -eq 'UNO_TX' -and $_.line -match '^C \d+ STOP\*'}) {
    $id=($t.line -split ' ')[1]; $d=@($events | Where-Object {$_.source -eq 'UNO_RX' -and $_.ms -ge $t.ms -and $_.line -eq "DONE $id MOTOR_STOPPED"})
    if($d.Count) {$stops += $d[0].ms-$t.ms}
  }
  $counts=@{}; foreach($e in $events) {$counts[$e.source]++}
  return @{eventCount=$events.Count;eventStartMs=$events[0].ms;eventEndMs=$events[-1].ms;requests=$tx.Count;complete=$done.Count;completeIds=$completeIds;ackMs=(Stats $ack);doneMs=(Stats $done);txIntervalMs=(Stats $interval);grantIntervalMs=(Stats $grantGaps);stopConfirmedMs=(Stats $stops);sources=$counts}
}
$old=Get-Content -LiteralPath $OldPath -Raw | ConvertFrom-Json
$new=Get-Content -LiteralPath "$PSScriptRoot/uart-stationary-capture.json" -Raw | ConvertFrom-Json
$marker=@($new.events | Where-Object {$_.source -eq 'MODE' -and $_.line -eq 'UART'})[0].ms
$result=@{analysisDate='2026-10-14';oldPath=$OldPath;oldSha256=(Get-FileHash -LiteralPath $OldPath -Algorithm SHA256).Hash;percentile='nearest-rank';old=(Analyze $old.logs);current=(Analyze ($new.events | Where-Object {$_.ms -ge $marker}));notes=@('Capture completeness is not a controlled UART loss rate.','TX-to-DONE excludes enqueue-to-TX waiting and physical stopping.')}
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$PSScriptRoot/latency-comparison.json" -Encoding utf8
$result | ConvertTo-Json -Depth 12
