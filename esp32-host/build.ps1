param([string]$Port='COM11',[switch]$Upload)
$ErrorActionPreference='Stop'
& node (Join-Path $PSScriptRoot 'generate-model.mjs')
if($LASTEXITCODE -ne 0){throw 'Model checksum or header generation failed'}
$cli='D:\software\arduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
if(!(Test-Path -LiteralPath $cli)){throw 'Set Arduino CLI path in build.ps1'}
$build=Join-Path $env:TEMP 'codex-esp32-cat-build'
$board='esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=max_app_8MB'
& $cli compile --fqbn $board --jobs 4 --build-path $build (Join-Path $PSScriptRoot 'ESP32CatHost')
if($LASTEXITCODE -ne 0){throw 'ESP32 compile failed'}
if($Upload){& $cli upload --fqbn $board --port $Port --input-dir $build;if($LASTEXITCODE -ne 0){throw 'ESP32 upload failed'}}
