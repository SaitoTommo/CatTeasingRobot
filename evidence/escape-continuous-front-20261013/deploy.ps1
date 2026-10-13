$ErrorActionPreference='Stop'
$ev=$PSScriptRoot
$tool='C:\Users\guoto\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe'
$app=Join-Path $ev 'ESP32_CAT_HOST_0.49_ESCAPE_CONTINUOUS_FRONT.bin'
$expected=Get-Content (Join-Path $ev 'validation.json') -Raw | ConvertFrom-Json
$state=Get-Content (Join-Path $ev 'esp-preflash-state.json') -Raw | ConvertFrom-Json
if($state.firmware -ne 'ESP32_CAT_HOST_0.48_ESCAPE_QUARTER_SEARCH' -or $state.phase -notin @('IDLE','STOPPED') -or $state.stopPendingId -ne 0 -or $state.stopUnconfirmed){throw 'Unexpected baseline or unsafe state'}
if($state.patrolStatus.motorId -or $state.patrolStatus.motorRunning -or $state.remote.active -or $state.remote.motorId){throw 'Motor or remote control active'}
if((Get-Item -LiteralPath $app).Length -ne $expected.espCompile.appFileBytes){throw 'Candidate file size mismatch'}
$part=Join-Path $ev 'esp-before-partitions.bin'
& $tool --chip esp32s3 --port COM11 --baud 460800 --before default-reset --after hard-reset read-flash 0x8000 0xC00 $part *> (Join-Path $ev 'partition-read.txt')
if($LASTEXITCODE -ne 0){throw 'Partition read failed'}
$compiled=Join-Path $env:TEMP 'codex-esp32-cat-build/ESP32CatHost.ino.partitions.bin'
$actualBytes=[IO.File]::ReadAllBytes($part)
$compiledBytes=[IO.File]::ReadAllBytes($compiled)
if($actualBytes.Length -ne $compiledBytes.Length){throw 'Partition length mismatch'}
for($i=0;$i -lt $actualBytes.Length;$i++){if($actualBytes[$i] -ne $compiledBytes[$i]){throw 'Partition layout mismatch'}}
$previous=Join-Path (Split-Path $ev -Parent) 'escape-quarter-search-20261013/ESP32_CAT_HOST_0.48_ESCAPE_QUARTER_SEARCH.bin'
$backup=Join-Path $ev 'esp-before-app.bin'
Copy-Item -LiteralPath $previous -Destination $backup
Write-Output 'Partition matched; existing known 0.48 application preserved for rollback (not a fresh device read).'
& $tool --chip esp32s3 --port COM11 --baud 460800 --before default-reset --after hard-reset write-flash 0x10000 $app *> (Join-Path $ev 'esp-upload.txt')
if($LASTEXITCODE -ne 0){throw 'Application flash failed'}
Write-Output 'Application flash completed; acceptance is firmware version readback per user request.'
