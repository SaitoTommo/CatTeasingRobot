$ErrorActionPreference='Stop'
$taskEvidence=$PSScriptRoot
$taskBase='http://192.168.137.234'
$taskTool='C:\Users\guoto\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe'
$taskApplication=Join-Path $taskEvidence 'ESP32_CAT_HOST_0.52_FRONT_AVOIDANCE_SWITCH.bin'
$taskTable=Join-Path $taskEvidence 'expected-partitions.bin'
$taskHash='D56CE47084D2BD49DEBC49F86F031B9B0C5ECB4CC37D4D8759FEF3E6A5A8BD81'
function Get-SafeState {
 $taskState=Invoke-RestMethod -Uri ($taskBase+'/api/state') -TimeoutSec 8
 foreach($taskField in @('firmware','deviceId','stopPendingId','stopUnconfirmed','patrolStatus','remote')) {
  if(!$taskState.PSObject.Properties[$taskField]){throw "Missing safety field: $taskField"}
 }
 if($taskState.firmware -ne 'ESP32_CAT_HOST_0.51_STRAIGHT_BALANCE_INTERVAL'){throw 'Unexpected baseline firmware'}
 if($taskState.phase -notin @('IDLE','STOPPED') -or $taskState.stopPendingId -ne 0 -or $taskState.stopUnconfirmed){throw 'Policy or STOP state unsafe'}
 if($taskState.patrolStatus.motorId -or $taskState.patrolStatus.motorRunning -or $taskState.patrolStatus.scanId -or $taskState.patrolStatus.interactionStopPending){throw 'Motion or interaction still pending'}
 if($taskState.remote.active -or $taskState.remote.busy -or $taskState.remote.motorId -or $taskState.remote.leftPwm -or $taskState.remote.rightPwm -or $taskState.debugBusy){throw 'Remote/debug activity present'}
 return $taskState
}
if((Get-FileHash -LiteralPath $taskApplication).Hash -ne $taskHash){throw 'Application hash changed'}
if((Get-Item -LiteralPath $taskApplication).Length -ne 3845328){throw 'Unexpected application size'}
if((Get-Item -LiteralPath $taskTable).Length -ne 3072){throw 'Unexpected partition table size'}
$taskBefore=Get-SafeState
$taskBefore | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $taskEvidence 'esp-before-state.json') -Encoding utf8
$taskArguments=@('--chip','esp32s3','--port','COM11','--baud','460800','--before','default-reset','--after','hard-reset')
$taskPartitionRead=Join-Path $taskEvidence 'esp-existing-partitions.bin'
$taskPartitionLog=Join-Path $taskEvidence 'partition-read.txt'
& $taskTool @taskArguments read-flash 0x8000 3072 $taskPartitionRead 2>&1 | Tee-Object -FilePath $taskPartitionLog
if($LASTEXITCODE -ne 0){throw 'Partition read failed; no write'}
if((Get-FileHash -LiteralPath $taskPartitionRead).Hash -ne (Get-FileHash -LiteralPath $taskTable).Hash){throw 'Partition table mismatch; no write'}
$taskPortText=Get-Content -LiteralPath $taskPartitionLog -Raw
$taskMac=[regex]::Match($taskPortText,'(?i)MAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})').Groups[1].Value
if(!$taskMac){throw 'COM11 MAC missing; no write'}
# Firmware prints the little-endian uint64_t returned by ESP.getEfuseMac().
$taskMacBytes=$taskMac.Split(':');[Array]::Reverse($taskMacBytes)
if(($taskMacBytes -join '') -ne $taskBefore.deviceId){throw 'HTTP device and COM11 MAC differ; no write'}
Write-Output 'Partition table and device identity matched. Waiting for baseline HTTP state after reset.'
$taskReady=$false
for($taskAttempt=0;$taskAttempt -lt 8;$taskAttempt++) {
 try {$taskPreflash=Get-SafeState;$taskReady=$true;break} catch {if($taskAttempt -eq 7){throw};Start-Sleep -Seconds 2}
}
if(!$taskReady -or $taskPreflash.deviceId -ne $taskBefore.deviceId){throw 'Device changed; no write'}
if(($taskPreflash.config | ConvertTo-Json -Depth 20 -Compress) -ne ($taskBefore.config | ConvertTo-Json -Depth 20 -Compress)){throw 'Saved configuration changed during preparation; no write'}
$taskPreflash | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $taskEvidence 'esp-preflash-state.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path (Split-Path $taskEvidence -Parent) 'straight-balance-interval-20261014\ESP32_CAT_HOST_0.51.bin') -Destination (Join-Path $taskEvidence 'known-rollback-0.51.bin')
Write-Output 'Writing only application at 0x10000; known 0.51 build retained for rollback (not a fresh device backup).'
$taskUploadLog=Join-Path $taskEvidence 'esp-upload.txt'
& $taskTool @taskArguments write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $taskApplication 2>&1 | Tee-Object -FilePath $taskUploadLog
if($LASTEXITCODE -ne 0){throw 'Application upload failed'}
if((Get-Content -LiteralPath $taskUploadLog -Raw) -notmatch 'Hash of data verified'){throw 'Upload did not confirm flash integrity'}
Write-Output 'Application upload and flash hash verification passed; HTTP version/configuration readback follows.'
