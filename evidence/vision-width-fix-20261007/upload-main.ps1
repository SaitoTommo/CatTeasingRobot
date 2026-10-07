$ErrorActionPreference='Stop'
$evidence=$PSScriptRoot
$state=& (Join-Path $evidence '../../evidence/camera-sensor-ranges-upload-20261006/get-esp-state.ps1') -OutputPath (Join-Path $evidence 'before-upload-state.json')
if($state.phase -notin @('STOPPED','IDLE') -or $state.patrolStatus.motorId -ne 0 -or $state.patrolStatus.motorRunning -or $state.stopPendingId -ne 0 -or $state.stopUnconfirmed -or $state.debugBusy){throw 'Host not stopped; no upload'}
$artifact=Get-Content -LiteralPath (Join-Path $evidence 'main-artifact.json') -Raw | ConvertFrom-Json
$application=$artifact.Path
if((Get-FileHash -LiteralPath $application).Hash -ne $artifact.Hash){throw 'Tested application hash mismatch'}
$table=Join-Path (Split-Path $application -Parent) 'ESP32CatHost.ino.partitions.bin'
$partitionBytes=[IO.File]::ReadAllBytes($table)
if($partitionBytes.Length -ne 3072){throw 'Unexpected compiled partition size'}
$uploader=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe'
$existing=Join-Path $evidence 'existing-partitions.bin'
$readOutput=& $uploader --chip esp32s3 --port COM11 --baud 115200 --before default-reset --after no-reset --no-stub --connect-attempts 3 read-flash 0x8000 3072 $existing 2>&1
$readCode=$LASTEXITCODE
$readOutput | Set-Content -LiteralPath (Join-Path $evidence 'partition-read.txt') -Encoding utf8
if($readCode -ne 0){throw 'Partition read failed; nothing written'}
$existingBytes=[IO.File]::ReadAllBytes($existing)
if($existingBytes.Length -ne $partitionBytes.Length){throw 'Partition length mismatch'}
for($index=0;$index -lt $partitionBytes.Length;$index++){if($existingBytes[$index] -ne $partitionBytes[$index]){throw 'Partition layout mismatch; nothing written'}}
$started=[DateTime]::UtcNow
$uploadOutput=& $uploader --chip esp32s3 --port COM11 --baud 460800 --before no-reset --after hard-reset --connect-attempts 3 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $application 2>&1
$uploadCode=$LASTEXITCODE
$uploadOutput | Set-Content -LiteralPath (Join-Path $evidence 'upload-main.txt') -Encoding utf8
if($uploadCode -ne 0 -or ($uploadOutput -join "`n") -notmatch 'Hash of data verified'){throw 'Upload or hash verification failed'}
[ordered]@{sourceCommit='b5a1af4';applicationSha256=$artifact.Hash;applicationBytes=(Get-Item -LiteralPath $application).Length;partitionBytes=3072;partitionMatches=$true;offset='0x10000';partitionBoard='esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=max_app_8MB';uploadExitCode=$uploadCode;elapsedSeconds=([DateTime]::UtcNow-$started).TotalSeconds;hashVerified=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'upload-result.json') -Encoding utf8
$uploadOutput | Select-Object -Last 7
