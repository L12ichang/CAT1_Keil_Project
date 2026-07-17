param(
  [string]$JLinkExe = "",
  [string]$Device = "STM32F103RC",
  [string]$Interface = "SWD",
  [int]$Speed = 4000,
  [Parameter(Mandatory = $true)][string]$Image,
  [Parameter(Mandatory = $true)][string]$Address,
  [ValidateSet("None", "ResetPin", "Software")][string]$ResetMode = "None",
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
$resetTypeCommand = ""
if ($ResetMode -eq "ResetPin") {
  $resetTypeCommand = "RSetType 2"
}

$postCommands = ""
if ($ResetMode -ne "None") {
  $postCommands = @"
r
g

"@
  if ($ProbeAfterRunMs -gt 0) {
    $postCommands += @"
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
h
verifybin $resolvedImage,$Address
$postCommands
exit
"@

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$scriptPath = Join-Path $logDir "jlink_verify_$stamp.jlink"
$logPath = Join-Path $logDir "jlink_verify_$stamp.log"
$script | Out-File -Encoding ascii -LiteralPath $scriptPath
& $JLinkExe -CommanderScript $scriptPath 2>&1 | Tee-Object -FilePath $logPath
$jlinkExitCode = $LASTEXITCODE
$logText = Get-Content -Raw -LiteralPath $logPath

if ($jlinkExitCode -ne 0) {
  throw "J-Link Commander failed with exit code $jlinkExitCode. See $logPath"
}
if ($logText -notmatch '(?m)^Verify successful\.$') {
  throw "J-Link verify did not succeed. See $logPath"
}
