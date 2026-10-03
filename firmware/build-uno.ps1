param([string]$BuildPath=(Join-Path $env:TEMP 'codex-uno-scan-build'))
$ErrorActionPreference='Stop'
$cli='D:/software/arduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
$flags='-mcall-prologues -mrelax -fno-split-wide-types -finline-limit=3'
& $cli compile --fqbn arduino:avr:uno --build-property "compiler.c.extra_flags=$flags" --build-property "compiler.cpp.extra_flags=$flags" --build-property "compiler.c.elf.extra_flags=$flags -Wl,--relax" --build-path $BuildPath (Join-Path $PSScriptRoot 'UnoCatController')
if($LASTEXITCODE -ne 0){throw 'Uno compile failed'}
# Deliberately offline. Upload and manual reset are separate operations.
