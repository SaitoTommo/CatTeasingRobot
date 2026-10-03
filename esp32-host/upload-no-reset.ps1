param(
 [Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
 [Parameter(Mandatory=$true)][switch]$ManualBootloaderReady,
 [string]$BuildPath=(Join-Path $env:TEMP 'codex-esp32-cat-build')
)
$ErrorActionPreference='Stop'
if(!$ManualBootloaderReady){throw 'The user must enter ROM download mode using BOOT/RESET before uploading.'}
$uploader=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe'
$application=Join-Path $BuildPath 'ESP32CatHost.ino.bin'
$table=Join-Path $BuildPath 'ESP32CatHost.ino.partitions.bin'
foreach($file in @($uploader,$application,$table)){if(!(Test-Path -LiteralPath $file)){throw "Missing: $file"}}
$existingTable=Join-Path $env:TEMP ("codex-esp-partitions-"+[guid]::NewGuid().ToString('N')+'.bin')
$partitionBytes=[IO.File]::ReadAllBytes($table)
# No automatic reset, no stub that could resume the application, bounded connect.
# Read and compare the existing layout before writing only the app; retain NVS.
$connection=@('--chip','esp32s3','--port',$Port,'--baud','115200','--before','no-reset','--after','no-reset','--no-stub','--connect-attempts','1')
& $uploader @connection read-flash 0x8000 $partitionBytes.Length $existingTable
if($LASTEXITCODE -ne 0){throw 'Partition read failed. No flash write; user handles recovery.'}
$existingBytes=[IO.File]::ReadAllBytes($existingTable)
if($existingBytes.Length -ne $partitionBytes.Length){throw 'Partition read length mismatch. No flash write.'}
for($index=0;$index -lt $partitionBytes.Length;$index++){
 if($partitionBytes[$index] -ne $existingBytes[$index]){throw 'Existing partition layout differs from the compiled max_app_8MB layout. No flash write; review with the user.'}
}
& $uploader @connection write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $application
if($LASTEXITCODE -ne 0){throw 'ESP upload failed. Do not reset automatically; user handles recovery.'}
Write-Output 'Application written and verified; NVS, bootloader and partitions not written. User presses RESET and performs acceptance.'
