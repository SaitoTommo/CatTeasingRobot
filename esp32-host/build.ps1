param([string]$Port='COM11',[switch]$Upload)
$ErrorActionPreference='Stop'
if($Upload){throw 'Automatic reset is not authorized. Build offline, then use the separate no-reset upload procedure with the user.'}
& node (Join-Path $PSScriptRoot 'generate-config.mjs')
if($LASTEXITCODE -ne 0){throw 'Config header generation failed'}
& node (Join-Path $PSScriptRoot 'generate-model.mjs')
if($LASTEXITCODE -ne 0){throw 'Model checksum or header generation failed'}
& node (Join-Path $PSScriptRoot 'generate-debug-page.mjs')
if($LASTEXITCODE -ne 0){throw 'Debug page compression failed'}
$cli='D:\software\arduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
if(!(Test-Path -LiteralPath $cli)){throw 'Set Arduino CLI path in build.ps1'}
$build=Join-Path $env:TEMP 'codex-esp32-cat-build'
$board='esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=max_app_8MB'
$sketch=Join-Path $env:TEMP 'codex-esp32-source/ESP32CatHost'
New-Item -ItemType Directory -Path $sketch -Force | Out-Null
$source=Join-Path $PSScriptRoot 'ESP32CatHost'
foreach($item in Get-ChildItem -LiteralPath $source -Recurse -File){
 $relative=[IO.Path]::GetRelativePath($source,$item.FullName)
 $target=Join-Path $sketch $relative
 $same=(Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $item.FullName).Hash
 if(!$same){New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null;Copy-Item -LiteralPath $item.FullName -Destination $target -Force}
}
& $cli compile --fqbn $board --jobs 4 --build-path $build $sketch
if($LASTEXITCODE -ne 0){throw 'ESP32 compile failed'}
