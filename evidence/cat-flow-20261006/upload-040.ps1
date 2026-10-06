$ErrorActionPreference='Stop'
$evidence=$PSScriptRoot
$state=Get-Content -LiteralPath (Join-Path $evidence 'before-state.json') -Raw | ConvertFrom-Json
if($state.phase -notin @('STOPPED','IDLE') -or $state.patrolStatus.motorId -ne 0 -or $state.patrolStatus.motorRunning -or $state.stopPendingId -ne 0 -or $state.stopUnconfirmed -or $state.debugBusy){throw 'ESP is not confirmed stopped; no upload performed'}
$expected=Get-Content -LiteralPath (Join-Path $evidence 'verification-040.json') -Raw | ConvertFrom-Json
$application=$expected.binaryPath
if((Get-FileHash -LiteralPath $application -Algorithm SHA256).Hash -ne $expected.binarySha256){throw 'Tested application hash mismatch'}
$build=Split-Path $application -Parent
$table=Join-Path $build 'ESP32CatHost.ino.partitions.bin'
$uploader=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe'
$existing=Join-Path $evidence 'existing-partitions.bin'
$partitionBytes=[IO.File]::ReadAllBytes($table)
if($partitionBytes.Length -ne 3072){throw 'Unexpected compiled partition length'}
$readOutput=& $uploader --chip esp32s3 --port COM11 --baud 115200 --before default-reset --after no-reset --no-stub --connect-attempts 3 read-flash 0x8000 3072 $existing 2>&1
$readCode=$LASTEXITCODE
$readOutput | Set-Content -LiteralPath (Join-Path $evidence 'partition-read.txt') -Encoding utf8
$readOutput
if($readCode -ne 0){throw 'Partition read failed; no flash written'}
$existingBytes=[IO.File]::ReadAllBytes($existing)
if($existingBytes.Length -ne $partitionBytes.Length){throw 'Partition length mismatch; no flash written'}
for($index=0;$index -lt $partitionBytes.Length;$index++){if($existingBytes[$index] -ne $partitionBytes[$index]){throw 'Partition layout mismatch; no flash written'}}
$started=[DateTime]::UtcNow
$uploadOutput=& $uploader --chip esp32s3 --port COM11 --baud 460800 --before no-reset --after hard-reset --connect-attempts 3 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $application 2>&1
$uploadCode=$LASTEXITCODE
$elapsed=([DateTime]::UtcNow-$started).TotalSeconds
$uploadOutput | Set-Content -LiteralPath (Join-Path $evidence 'upload.txt') -Encoding utf8
$uploadOutput
if($uploadCode -ne 0 -or ($uploadOutput -join "`n") -notmatch 'Hash of data verified'){throw 'Application upload or hash verification failed'}
[ordered]@{applicationSha256=$expected.binarySha256;applicationBytes=(Get-Item -LiteralPath $application).Length;partitionBytes=3072;partitionMatches=$true;offset='0x10000';uploadExitCode=$uploadCode;elapsedSeconds=$elapsed;hashVerified=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'upload-result.json') -Encoding utf8


