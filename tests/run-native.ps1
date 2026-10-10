param(
 [string]$Compiler=(Join-Path $env:TEMP 'codex-zig-0.14.1/zig-x86_64-windows-0.14.1/zig.exe'),
 [string]$ArduinoJson='D:/data/documents/Arduino/libraries/ArduinoJson/src'
)
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $Compiler)){throw 'Supply -Compiler with Zig 0.14.1 zig.exe (portable; no hardware access)'}
$motionSource=Join-Path $env:TEMP 'codex-uno-motion-source.inc'
node (Join-Path $PSScriptRoot 'uno-balance-source.mjs') $motionSource
if($LASTEXITCODE -ne 0){throw 'Uno motion extraction failed'}
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $env:TEMP 'codex-zig-cache'
foreach($name in @('scan-machine','vision-gate','vision-preview','wire-integrity','policy-core','cat-check','tag-scan','tag-recovery','approach-front','cat-recovery','cat-interaction','action-resume','pwm-floor','camera-settings','camera-runtime','ground-calibration')){
 $binary=Join-Path $env:TEMP "codex-$name-test.exe"
 & $Compiler c++ -std=c++20 -O1 -target x86_64-windows-gnu -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I (Join-Path $PSScriptRoot 'native') -I $ArduinoJson (Join-Path $PSScriptRoot "$name.test.cpp") -o $binary
 if($LASTEXITCODE -ne 0){throw "$name compile failed"}
 & $binary
 if($LASTEXITCODE -ne 0){throw "$name assertions failed"}
}
$binary=Join-Path $env:TEMP 'codex-uno-straight-test.exe'
& $Compiler c++ -std=c++20 -O1 -target x86_64-windows-gnu ('-DUNO_MOTION_SOURCE="'+$motionSource.Replace('\','/')+'"') (Join-Path $PSScriptRoot 'uno-straight.test.cpp') -o $binary
if($LASTEXITCODE -ne 0){throw 'Uno straight integration compile failed'}
& $binary
if($LASTEXITCODE -ne 0){throw 'Uno straight integration assertions failed'}
$stopOutputSource=Join-Path $env:TEMP 'codex-stop-output-source.inc'
node (Join-Path $PSScriptRoot 'stop-output-source.mjs') $stopOutputSource
if($LASTEXITCODE -ne 0){throw 'STOP output source extraction failed'}
$binary=Join-Path $env:TEMP 'codex-stop-output-test.exe'
& $Compiler c++ -std=c++20 -O1 -target x86_64-windows-gnu ('-DSTOP_OUTPUT_SOURCE="'+$stopOutputSource.Replace('\','/')+'"') -I (Join-Path $PSScriptRoot 'native') (Join-Path $PSScriptRoot 'stop-output.test.cpp') -o $binary
if($LASTEXITCODE -ne 0){throw 'STOP output integration compile failed'}
& $binary
if($LASTEXITCODE -ne 0){throw 'STOP output integration assertions failed'}
