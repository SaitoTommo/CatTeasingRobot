param([string]$OutputPath='')
$ErrorActionPreference='Stop'
$port=[System.IO.Ports.SerialPort]::new('COM11',115200)
$port.Encoding=[Text.Encoding]::UTF8;$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadTimeout=500
$state=$null
try{
 $port.Open();$port.WriteLine('{"op":"state"}');$until=[DateTime]::UtcNow.AddSeconds(6)
 while([DateTime]::UtcNow -lt $until){
  try{$line=$port.ReadLine();if($line.StartsWith('{"firmware"')){$state=$line | ConvertFrom-Json;break}}catch [System.TimeoutException]{}
 }
}finally{if($port.IsOpen){$port.Close()};$port.Dispose()}
if(!$state){throw 'No ESP32 state reply; no motion or reset sent'}
if($OutputPath){$state | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $OutputPath -Encoding utf8}
$state

