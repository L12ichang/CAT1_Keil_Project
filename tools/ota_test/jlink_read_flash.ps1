param(
  [string]$JLinkExe = "",
  [string]$Device = "STM32F103RC",
  [string]$Interface = "SWD",
  [int]$Speed = 4000,
  [string]$AppStart = "0x08008000",
  [string]$OtaStart = "0x08024000",
  [string]$DataStart = "0x08005000",
  [string]$Length = "0x400"
)

$ErrorActionPreference = "Stop"
$logDir = "tools\ota_test\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

if (-not $JLinkExe) {
  if (Test-Path "D:\Keil_v5\ARM\Segger\JLink.exe") {
    $JLinkExe = "D:\Keil_v5\ARM\Segger\JLink.exe"
  } elseif (Test-Path "C:\Program Files (x86)\SEGGER\JLink_V810\JLink.exe") {
    $JLinkExe = "C:\Program Files (x86)\SEGGER\JLink_V810\JLink.exe"
  } else {
    throw "JLink.exe not found"
  }
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$appHead = Join-Path $logDir "app_head_$stamp.bin"
$otaHead = Join-Path $logDir "ota_backup_head_$stamp.bin"
$dataHead = Join-Path $logDir "data_head_$stamp.bin"
$script = @"
device $Device
if $Interface
speed $Speed
connect
mem16 0x1FFFF7E0 1
savebin $appHead,$AppStart,$Length
savebin $otaHead,$OtaStart,$Length
savebin $dataHead,$DataStart,$Length
exit
"@

$scriptPath = Join-Path $logDir "jlink_read_flash_$stamp.jlink"
$logPath = Join-Path $logDir "jlink_read_flash_$stamp.log"
$script | Out-File -Encoding ascii -LiteralPath $scriptPath
& $JLinkExe -CommanderScript $scriptPath 2>&1 | Tee-Object -FilePath $logPath
