$ErrorActionPreference='Stop'
function Stats($values) {
  $v=@($values|Sort-Object); if(!$v.Count){return $null}
  $i=[int][Math]::Floor($v.Count/2)
  $median=if($v.Count%2){$v[$i]}else{($v[$i-1]+$v[$i])/2}
  @{n=$v.Count;min=$v[0];median=$median;p95=$v[[int][Math]::Ceiling(.95*$v.Count)-1];max=$v[-1]}
}
$capture=Get-Content -LiteralPath "$PSScriptRoot/uart-stationary-capture.json" -Raw|ConvertFrom-Json
$mode=@($capture.events|Where-Object {$_.source -eq 'MODE' -and $_.line -eq 'UART'})[0].ms
$e=@($capture.events|Where-Object {$_.ms -ge $mode}|Sort-Object ms,seq)
$rows=@()
foreach($tx in $e|Where-Object {$_.source -eq 'UART_WINDOW_TX'}){
  if($tx.line -notmatch '^R (\d+) C (\d+) (.+)\*[0-9A-F]{2}$'){throw 'Unexpected window line'}
  $window=$Matches[1];$id=$Matches[2];$command=$Matches[3]
  $grant=@($e|Where-Object {$_.source -eq 'UART_AVAILABLE' -and $_.line -eq "RX $window" -and $_.ms -le $tx.ms}|Select-Object -Last 1)
  $ack=@($e|Where-Object {$_.source -eq 'UNO_RX' -and $_.line -eq "AK $id" -and $_.ms -ge $tx.ms}|Select-Object -First 1)
  $done=@($e|Where-Object {$_.source -eq 'UNO_RX' -and $_.line -match "^DONE $id( |$)" -and $_.ms -ge $tx.ms}|Select-Object -First 1)
  $row=[ordered]@{id=[int]$id;window=[int]$window;command=$command;txMs=$tx.ms;grantMs=$null;grantToTxMs=$null;grantToAckMs=$null;grantToDoneMs=$null;txToAckMs=$null;txToDoneMs=$null}
  if($grant.Count){$row.grantMs=$grant[0].ms;$row.grantToTxMs=$tx.ms-$grant[0].ms}
  if($ack.Count){$row.txToAckMs=$ack[0].ms-$tx.ms;if($grant.Count){$row.grantToAckMs=$ack[0].ms-$grant[0].ms}}
  if($done.Count){$row.txToDoneMs=$done[0].ms-$tx.ms;if($grant.Count){$row.grantToDoneMs=$done[0].ms-$grant[0].ms}}
  $rows += [pscustomobject]$row
}
$telemetry=@($rows|Where-Object {$_.command -eq 'GET_TELEMETRY'})
$triggerRows=@()
foreach($id in @(31,32,33)) {
  $tx=@($e|Where-Object {$_.source -eq 'UNO_TX' -and $_.line -match "^C $id "})[0]
  $ack=@($e|Where-Object {$_.source -eq 'UNO_RX' -and $_.line -eq "AK $id"})
  $done=@($e|Where-Object {$_.source -eq 'UNO_RX' -and $_.line -match "^DONE $id( |$)"})
  $row=[ordered]@{id=$id;command=($tx.line -replace '^C \d+ ','') -replace '\*[0-9A-F]{2}$','';triggerToTxMs=$tx.ms-$mode;triggerToAckMs=$null;triggerToDoneMs=$null}
  if($ack.Count){$row.triggerToAckMs=$ack[0].ms-$mode}
  if($done.Count){$row.triggerToDoneMs=$done[0].ms-$mode}
  if($id -eq 32){$row.triggerToErrorMs=(@($e|Where-Object {$_.source -eq 'UNO_RX' -and $_.line -eq 'ERROR 0 RX_WINDOW_EXPIRED'})[0].ms)-$mode}
  $triggerRows += [pscustomobject]$row
}
$result=[ordered]@{modeTriggerMs=$mode;telemetryGrantToTxMs=(Stats $telemetry.grantToTxMs);telemetryGrantToAckMs=(Stats $telemetry.grantToAckMs);telemetryGrantToDoneMs=(Stats $telemetry.grantToDoneMs);transactions=$rows;modeTriggeredCommands=$triggerRows;notes=@('MODE is trigger proxy, logged just after these commands were enqueued in the same handler; not per-command enqueue timestamp.','Grant-to-TX/ACK/DONE still excludes waiting before the selected grant.','ACK is host-observed acknowledgement, not precise Uno instruction-start or physical-output timing.','No per-command enqueue timestamp exists in either capture. No exact enqueue-to-DONE distribution can be recovered.')}
$result|ConvertTo-Json -Depth 9|Set-Content -LiteralPath "$PSScriptRoot/dispatch-comparison.json" -Encoding utf8
$result|ConvertTo-Json -Depth 9
