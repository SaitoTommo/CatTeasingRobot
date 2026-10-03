param(
 [Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
 [Parameter(Mandatory=$true)][switch]$ManualBootloaderReady,
 [string]$BuildPath=(Join-Path $env:TEMP 'codex-uno-scan-build')
)
$ErrorActionPreference='Stop'
if(!$ManualBootloaderReady){throw 'The user must prepare the board and manually reset into the bootloader.'}
$toolRoot=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/arduino/tools/avrdude/8.0.0-arduino1'
$uploader=Join-Path $toolRoot 'bin/avrdude.exe'
$configuration=Join-Path $toolRoot 'etc/avrdude.conf'
$application=Join-Path $BuildPath 'UnoCatController.ino.hex'
foreach($file in @($uploader,$configuration,$application)){if(!(Test-Path -LiteralPath $file)){throw "Missing: $file"}}
# Installed AVRDUDE 8 help explicitly documents noautoreset for arduino.
# One connection attempt only. User presses RESET; this script does not retry/reset.
& $uploader -C $configuration -p atmega328p -c arduino -P $Port -b 115200 -x noautoreset -x attempts=1 -D -U "flash:w:${application}:i"
if($LASTEXITCODE -ne 0){throw 'Uno upload failed. Leave stopped; user handles reset/recovery.'}
Write-Output 'Application written and verified. No bootloader/fuse write. User performs restart and acceptance.'
