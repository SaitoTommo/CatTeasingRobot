$ErrorActionPreference='Stop'
$taskBase='http://192.168.137.234'
$taskBefore=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'esp-preflash-state.json') -Raw | ConvertFrom-Json
$taskExpected='ESP32_CAT_HOST_0.52_FRONT_AVOIDANCE_SWITCH'
$taskAfter=$null
for($taskAttempt=0;$taskAttempt -lt 8;$taskAttempt++) {
 try {
  $taskCandidate=Invoke-RestMethod -Uri ($taskBase+'/api/state') -TimeoutSec 5
  if($taskCandidate.firmware -ne $taskExpected){throw 'Waiting for expected firmware'}
  $taskAfter=$taskCandidate;break
 } catch {if($taskAttempt -eq 7){throw};Start-Sleep -Seconds 2}
}
if(!$taskAfter){throw 'No post-upload state'}
$taskAfter | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'esp-after-state.json') -Encoding utf8
if($taskAfter.deviceId -ne $taskBefore.deviceId){throw 'Device identity changed'}
if($taskAfter.phase -notin @('IDLE','STOPPED') -or $taskAfter.mode -ne 'SIM_UNO' -or $taskAfter.stopPendingId -ne 0 -or $taskAfter.stopUnconfirmed){throw 'Post-upload policy/stop state unexpected'}
if($taskAfter.patrolStatus.motorId -or $taskAfter.patrolStatus.motorRunning -or $taskAfter.patrolStatus.scanId -or $taskAfter.remote.active -or $taskAfter.remote.busy -or $taskAfter.remote.motorId -or $taskAfter.debugBusy){throw 'Post-upload activity unexpected'}
if(!$taskAfter.cameraOk){throw 'Camera did not initialize'}
$taskChanged=@()
foreach($taskProperty in $taskBefore.config.PSObject.Properties){
 $taskName=$taskProperty.Name
 if(!$taskAfter.config.PSObject.Properties[$taskName] -or ($taskProperty.Value | ConvertTo-Json -Depth 20 -Compress) -ne ($taskAfter.config.$taskName | ConvertTo-Json -Depth 20 -Compress)){$taskChanged+=$taskName}
}
foreach($taskProperty in $taskAfter.config.PSObject.Properties){if(!$taskBefore.config.PSObject.Properties[$taskProperty.Name]){$taskChanged+=$taskProperty.Name}}
if($taskChanged.Count){throw ('Saved settings changed: '+($taskChanged -join ', '))}
$taskHandler=[Net.Http.HttpClientHandler]::new()
$taskHandler.AutomaticDecompression=[Net.DecompressionMethods]::None
$taskClient=[Net.Http.HttpClient]::new($taskHandler)
$taskClient.Timeout=[TimeSpan]::FromSeconds(8)
try {
 $taskResponse=$taskClient.GetAsync($taskBase+'/').GetAwaiter().GetResult()
 $null=$taskResponse.EnsureSuccessStatusCode()
 if($taskResponse.Content.Headers.ContentEncoding -notcontains 'gzip'){throw 'Expected gzip console response'}
 $taskPage=$taskResponse.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
 [IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'served-page.gz'),$taskPage)
} finally {$taskClient.Dispose();$taskHandler.Dispose()}
$taskHeader=Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\esp32-host\ESP32CatHost\debug_page.h') -Raw
$taskArray=[regex]::Match($taskHeader,'(?s)PROGMEM=\{(.*?)\};').Groups[1].Value
if(!$taskArray){throw 'Generated page byte array missing'}
$taskExpectedPage=[byte[]]($taskArray.Split(',') | ForEach-Object {[byte]$_.Trim()})
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'expected-page.gz'),$taskExpectedPage)
$taskPageHash=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'served-page.gz')).Hash
if($taskPageHash -ne (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'expected-page.gz')).Hash){throw 'Served console differs from compiled console'}
$taskSummary=[ordered]@{
 sourceCommit=(git -C (Join-Path $PSScriptRoot '..\..') rev-parse HEAD)
 verifiedAtUtc=[DateTimeOffset]::UtcNow.ToString('o')
 firmware=$taskAfter.firmware;deviceId=$taskAfter.deviceId;stationIP=$taskAfter.stationIP
 phase=$taskAfter.phase;mode=$taskAfter.mode;cameraOk=$taskAfter.cameraOk
 motorRunning=$taskAfter.patrolStatus.motorRunning;stopPendingId=$taskAfter.stopPendingId;stopUnconfirmed=$taskAfter.stopUnconfirmed
 savedConfigFields=@($taskBefore.config.PSObject.Properties).Count;savedConfigChanges=0
 route=$taskAfter.config.route;frontAvoidanceEnabled=$taskAfter.config.frontAvoidanceEnabled;frontCalibrated=$taskAfter.config.frontCalibrated
 straightBalanceIntervalMs=$taskAfter.config.straightBalanceIntervalMs
 port='COM11';appOffset='0x10000';appFileBytes=3845328
 appSha256='D56CE47084D2BD49DEBC49F86F031B9B0C5ECB4CC37D4D8759FEF3E6A5A8BD81'
 partitionBytesCompared=3072;partitionMatched=$true;flashHashVerified=$true
 servedPageMatches=$true;servedPageSha256=$taskPageHash
 programStorageBytes=3845178;staticRamBytes=86032
 nativeChecks=@{escapeSearch=344;frontSwitchPersistence=8;policyCore=1245;frontApproach=144}
 nvsWritten=$false;partitionTableWritten=$false;bootloaderWritten=$false;unoUploaded=$false;physicalMotionTested=$false
 rollbackArtifact='known-rollback-0.51.bin';rollbackIsFreshDeviceRead=$false
}
$taskSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'verification.json') -Encoding utf8
$taskSummary | ConvertTo-Json -Depth 10
