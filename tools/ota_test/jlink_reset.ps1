param(
  [string]$JLinkExe = "",
  [string]$Device = "STM32F103RC",
  [string]$Interface = "SWD",
  [int]$Speed = 4000
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

$script = @"
device $Device
if $Interface
speed $Speed
connect
r
g
exit
"@

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$scriptPath = Join-Path $logDir "jlink_reset_$stamp.jlink"
$logPath = Join-Path $logDir "jlink_reset_$stamp.log"
$script | Out-File -Encoding ascii -LiteralPath $scriptPath
& $JLinkExe -CommanderScript $scriptPath 2>&1 | Tee-Object -FilePath $logPath
