param(
  [string]$JLinkExe = "",
  [string]$Device = "STM32F103RC",
  [string]$Interface = "SWD",
  [int]$Speed = 4000,
  [Parameter(Mandatory = $true)][string]$Image,
  [string]$Address = "",
  [ValidateSet("ResetPin", "Software", "None")][string]$PostFlashReset = "ResetPin",
  [int]$ProbeAfterRunMs = 0
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

$resolvedImage = (Resolve-Path $Image).Path
$ext = [System.IO.Path]::GetExtension($resolvedImage).ToLowerInvariant()
if ($ext -eq ".bin") {
  if (-not $Address) {
    throw "Address is required when flashing a .bin file"
  }
  $loadCommand = "loadbin $resolvedImage,$Address"
  $verifyCommand = "verifybin $resolvedImage,$Address"
} else {
  $loadCommand = "loadfile $resolvedImage"
  $verifyCommand = ""
}

$resetTypeCommand = ""
if ($PostFlashReset -eq "ResetPin") {
  $resetTypeCommand = "RSetType 2"
}

$postFlashCommands = ""
if ($PostFlashReset -ne "None") {
  $postFlashCommands = @"
r
g
"@
  if ($ProbeAfterRunMs -gt 0) {
    $postFlashCommands += "`n"
    $postFlashCommands += @"
Sleep $ProbeAfterRunMs
h
regs
mem32 0xE000ED08 1
mem32 0xE000ED28 1
mem32 0xE000ED2C 1
mem32 0x40021024 1
"@
  }
}

$script = @"
device $Device
if $Interface
speed $Speed
connect
$resetTypeCommand
r
h
$loadCommand
$verifyCommand
$postFlashCommands
exit
"@

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$scriptPath = Join-Path $logDir "jlink_flash_$stamp.jlink"
$logPath = Join-Path $logDir "jlink_flash_$stamp.log"
$script | Out-File -Encoding ascii -LiteralPath $scriptPath
& $JLinkExe -CommanderScript $scriptPath 2>&1 | Tee-Object -FilePath $logPath
