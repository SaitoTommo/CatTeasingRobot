param([string]$RestoreProfile='VIC-11V4L7RW4GG')
$ErrorActionPreference='Stop'
$profile=Join-Path $PSScriptRoot 'vendor\esp32-hotspot.xml'
@'
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1"><name>ESP32-CatHost</name><SSIDConfig><SSID><name>ESP32-CatHost</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>catrobot32</keyMaterial></sharedKey></security></MSM></WLANProfile>
'@ | Set-Content -LiteralPath $profile -Encoding utf8
& netsh wlan add profile "filename=$profile" user=current
if($LASTEXITCODE -ne 0){throw 'Cannot add ESP32 hotspot profile'}
try {
  & netsh wlan connect name=ESP32-CatHost interface=WLAN
  if($LASTEXITCODE -ne 0){throw 'Cannot connect ESP32 hotspot'}
  $ready=$false
  for($attempt=0;$attempt -lt 15;$attempt++) {
    Start-Sleep -Seconds 2
    $addresses=Get-NetIPAddress -InterfaceAlias WLAN -AddressFamily IPv4 -ErrorAction SilentlyContinue
    if($addresses | Where-Object { $_.IPAddress -like '192.168.4.*' }){$ready=$true;break}
  }
  & netsh wlan show interfaces
  if(!$ready){throw 'ESP32 hotspot DHCP address not acquired'}
  & 'C:\Users\guoto\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' (Join-Path $PSScriptRoot 'http-smoke.py')
  if($LASTEXITCODE -ne 0){throw 'HTTP camera verification failed'}
} finally {
  & netsh wlan connect "name=$RestoreProfile" interface=WLAN
  Start-Sleep -Seconds 3
  & netsh wlan show interfaces
}
