param(
 [string]$Compiler=(Join-Path $env:TEMP 'codex-zig-0.14.1/zig-x86_64-windows-0.14.1/zig.exe'),
 [string]$ArduinoJson='D:/data/documents/Arduino/libraries/ArduinoJson/src'
)
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $Compiler)){throw 'Supply -Compiler with Zig 0.14.1 zig.exe (portable; no hardware access)'}
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $env:TEMP 'codex-zig-cache'
foreach($name in @('scan-machine','vision-gate','wire-integrity','policy-core')){
 $binary=Join-Path $env:TEMP "codex-$name-test.exe"
 & $Compiler c++ -std=c++20 -O1 -target x86_64-windows-gnu -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I (Join-Path $PSScriptRoot 'native') -I $ArduinoJson (Join-Path $PSScriptRoot "$name.test.cpp") -o $binary
 if($LASTEXITCODE -ne 0){throw "$name compile failed"}
 & $binary
 if($LASTEXITCODE -ne 0){throw "$name assertions failed"}
}
