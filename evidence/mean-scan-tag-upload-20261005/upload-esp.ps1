$ErrorActionPreference='Stop'
$evidence=$PSScriptRoot
$state=& (Join-Path $evidence '../camera-preview-layout-20261005/get-esp-state.ps1') -OutputPath (Join-Path $evidence 'before-state.json')
if($state.phase -notin @('STOPPED','IDLE') -or $state.patrolStatus.motorId -ne 0 -or $state.patrolStatus.motorRunning -or $state.stopPendingId -ne 0 -or $state.stopUnconfirmed -or $state.debugBusy){throw 'ESP is not confirmed stopped; no upload performed'}
$build=Join-Path $env:TEMP 'codex-esp32-cat-build'
$application=Join-Path $build 'ESP32CatHost.ino.bin'
$table=Join-Path $build 'ESP32CatHost.ino.partitions.bin'
$uploader=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe'
$existing=Join-Path $evidence 'existing-partitions.bin'
Get-FileHash -LiteralPath $application -Algorithm SHA256 | Format-List | Out-String | Set-Content -LiteralPath (Join-Path $evidence 'application-sha256.txt')
$partitionBytes=[IO.File]::ReadAllBytes($table)
& $uploader --chip esp32s3 --port COM11 --baud 115200 --before default-reset --after no-reset --no-stub --connect-attempts 3 read-flash 0x8000 $partitionBytes.Length $existing 2>&1 | Tee-Object -FilePath (Join-Path $evidence 'partition-read.txt')
if($LASTEXITCODE -ne 0){throw 'Partition read failed; no flash written'}
$existingBytes=[IO.File]::ReadAllBytes($existing)
if($existingBytes.Length -ne $partitionBytes.Length){throw 'Partition length mismatch; no flash written'}
for($index=0;$index -lt $partitionBytes.Length;$index++){if($existingBytes[$index] -ne $partitionBytes[$index]){throw 'Partition layout mismatch; no flash written'}}
& $uploader --chip esp32s3 --port COM11 --baud 460800 --before no-reset --after hard-reset --connect-attempts 3 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $application 2>&1 | Tee-Object -FilePath (Join-Path $evidence 'upload.txt')
if($LASTEXITCODE -ne 0){throw 'Application upload failed'}
