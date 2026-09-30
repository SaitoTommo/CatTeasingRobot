param([ValidateRange(1024,65535)][int]$Port=8765,[switch]$NoBrowser)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$base="http://127.0.0.1:$Port"
$url="$base/esp32-lab.html"

function Test-Esp32Page {
  try {
    $page=Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 2
    $link=Invoke-WebRequest -Uri "$base/esp32-link.mjs" -UseBasicParsing -TimeoutSec 2
    $app=Invoke-WebRequest -Uri "$base/esp32-lab.mjs" -UseBasicParsing -TimeoutSec 2
    return ($page.StatusCode -eq 200 -and $page.Content.Contains('data-vision="OFF"') -and $link.StatusCode -eq 200 -and $app.StatusCode -eq 200)
  } catch {return $false}
}

try {
  if(!(Test-Esp32Page)) {
    if(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
      throw "Port $Port is occupied by another or older server. Close that server, then run this launcher again."
    }
    $node=(Get-Command node.exe -ErrorAction SilentlyContinue).Source
    if(!$node -and (Test-Path -LiteralPath 'D:\software\nodejs\node.exe')){$node='D:\software\nodejs\node.exe'}
    if(!$node){throw 'Node.js was not found. Install Node.js or add it to PATH.'}
    $logs=Join-Path $PSScriptRoot 'vendor'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $oldPort=$env:UNO_WEB_LAB_PORT
    try {
      $env:UNO_WEB_LAB_PORT="$Port"
      $server=Start-Process -FilePath $node -ArgumentList 'server.mjs' -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logs "web-$Port.log") -RedirectStandardError (Join-Path $logs "web-$Port.error.log") -PassThru
    } finally {$env:UNO_WEB_LAB_PORT=$oldPort}
    $ready=$false
    for($attempt=0;$attempt -lt 20;$attempt++) {
      if(Test-Esp32Page){$ready=$true;break}
      if($server.HasExited){break}
      Start-Sleep -Milliseconds 500
    }
    if(!$ready){throw "Server did not start. See $logs\web-$Port.error.log"}
    Write-Host "ESP32 web server started. PID: $($server.Id)"
    Write-Host "To stop it: Stop-Process -Id $($server.Id)"
  } else {Write-Host 'Reusing the running ESP32 web server.'}
  Write-Host "Open: $url"
  Write-Host 'USB: select COM11 in the page. Video: connect the ESP32-CatHost hotspot after attaching the antenna.'
  if(!$NoBrowser){Start-Process $url}
} catch {
  Write-Host $_.Exception.Message -ForegroundColor Red
  exit 1
}
