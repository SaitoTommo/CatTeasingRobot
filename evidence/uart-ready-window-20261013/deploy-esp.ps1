$ErrorActionPreference='Stop'
$uploader='C:/Users/guoto/AppData/Local/Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe'
$build=Join-Path $env:TEMP 'codex-esp32-cat-build'
$table=Join-Path $build 'ESP32CatHost.ino.partitions.bin'
$app=Join-Path $build 'ESP32CatHost.ino.bin'
$existing=Join-Path $env:TEMP 'codex-esp-window-partition-check.bin'
$log=Join-Path $PSScriptRoot 'esp-upload.txt'
$arguments=@('--chip','esp32s3','--port','COM11','--baud','460800','--before','default-reset','--after','hard-reset')
& $uploader @arguments read-flash 0x8000 3072 $existing 2>&1 | Tee-Object -FilePath $log
if($LASTEXITCODE -ne 0){throw 'Partition compatibility read failed; no write'}
if((Get-FileHash -LiteralPath $existing).Hash -ne (Get-FileHash -LiteralPath $table).Hash){throw 'Partition layout differs; no write'}
'Partition layout matches; write app only at 0x10000' | Tee-Object -FilePath $log -Append
& $uploader @arguments write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $app 2>&1 | Tee-Object -FilePath $log -Append
if($LASTEXITCODE -ne 0){throw 'ESP application write/verify failed'}
'ESP upload exit=0; esptool verified write; NVS/partition/bootloader not written' | Tee-Object -FilePath $log -Append
