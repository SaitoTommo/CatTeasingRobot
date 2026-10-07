param([string]$OutputName='before-uno')
$ErrorActionPreference='Stop'
function Crc([string]$text){$value=0;foreach($b in [Text.Encoding]::ASCII.GetBytes($text)){$value=$value -bxor $b;for($i=0;$i -lt 8;$i++){$value=if($value -band 128){(($value -shl 1) -bxor 7) -band 255}else{($value -shl 1) -band 255}}};return $value}
$serial=[IO.Ports.SerialPort]::new('COM10',57600);$serial.DtrEnable=$false;$serial.RtsEnable=$false;$serial.ReadTimeout=100
$raw=[Collections.Generic.List[string]]::new();$bad=0
function Query([int]$id,[string]$command,[string]$terminal){
 $payload="C $id $command";$serial.WriteLine($payload+('*{0:X2}' -f (Crc $payload)))
 $checked=[Collections.Generic.List[string]]::new();$until=[DateTime]::UtcNow.AddSeconds(6)
 while([DateTime]::UtcNow -lt $until){
  try{$line=$serial.ReadLine().TrimEnd("`r");$raw.Add($line)}catch [TimeoutException]{continue}
  if($line -match '^(.*)\*([0-9A-Fa-f]{2})$'){$body=$Matches[1];$expected=[Convert]::ToInt32($Matches[2],16);if((Crc $body) -ne $expected){$script:bad++;continue};$checked.Add($body);if($body -eq $terminal){return $checked.ToArray()}}
 }
 throw "No checked terminal reply for $command"
}
function Telemetry([int]$id){
 $lines=Query $id 'GET_TELEMETRY' "DONE $id TELEMETRY";$parts=@{}
 foreach($line in $lines){if($line -match "^TB $id ([0-2]) ([0-9A-F]+)$"){$parts[[int]$Matches[1]]=$Matches[2]}}
 if($parts.Count -ne 3 -or $parts[0].Length -ne 34 -or $parts[1].Length -ne 34 -or $parts[2].Length -ne 32){throw 'Incomplete telemetry'}
 $hex=$parts[0]+$parts[1]+$parts[2];$bytes=[byte[]]::new(50);for($i=0;$i -lt 50;$i++){$bytes[$i]=[Convert]::ToByte($hex.Substring($i*2,2),16)}
 $requested=@([BitConverter]::ToInt16($bytes,8),[BitConverter]::ToInt16($bytes,10));$applied=@([BitConverter]::ToInt16($bytes,12),[BitConverter]::ToInt16($bytes,14))
 if(($bytes[5] -band 2) -or $requested[0] -or $requested[1] -or $applied[0] -or $applied[1]){throw 'Uno not stopped'}
 return [ordered]@{ms=[BitConverter]::ToUInt32($bytes,1);sessionStopped=[bool]($bytes[5] -band 1);motionActive=[bool]($bytes[5] -band 2);requested=$requested;applied=$applied;leftCount=[BitConverter]::ToUInt32($bytes,18);rightCount=[BitConverter]::ToUInt32($bytes,22);ioMask=[BitConverter]::ToUInt16($bytes,6)}
}
try{
 $serial.Open()
 # Opening this board's USB adapter can restart the sketch. Drain startup
 # before sending the first STOP; never infer a stop from a boot message.
 $startup=[DateTime]::UtcNow.AddSeconds(3)
 while([DateTime]::UtcNow -lt $startup){try{$raw.Add($serial.ReadLine().TrimEnd("`r"))}catch [TimeoutException]{}}
 $stop=Query 64001 'STOP' 'DONE 64001 MOTOR_STOPPED'
 $session=Query 64002 'SESSION_STOP' 'DONE 64002 SESSION_STOPPED'
 $ping=Query 64003 'PING' 'DONE 64003 PONG'
 $configuration=Query 64004 'GET_CONFIG' 'DONE 64004'
 $first=Telemetry 64005
 Start-Sleep -Milliseconds 2000
 $second=Telemetry 64006
 [ordered]@{port='COM10';stopConfirmed=$true;sessionStopped=$true;pingConfirmed=$true;config=@($raw | Where-Object {$_ -like 'DATA 64004 CONFIG *'});first=$first;second=$second;stationaryCountsEqual=($first.leftCount -eq $second.leftCount -and $first.rightCount -eq $second.rightCount);badCheckedLines=$bad;ready=@($raw | Where-Object {$_ -like 'READY *'})} | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $PSScriptRoot "$OutputName.json") -Encoding utf8
 $raw | Set-Content (Join-Path $PSScriptRoot "$OutputName.txt") -Encoding utf8
 Write-Output 'STOP, SESSION_STOP, PING and two complete stopped telemetry snapshots verified.'
}finally{if($serial.IsOpen){$serial.Close()};$serial.Dispose();if($raw.Count){$raw | Set-Content (Join-Path $PSScriptRoot "$OutputName.txt") -Encoding utf8}}
