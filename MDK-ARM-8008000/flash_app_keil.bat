@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "BOOT_BIN=D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\boot.bin"
set "APP_BIN=%~dp0out\CAT1_50W.bin"
set "MERGED_BIN=%~dp0out\boot_app_merged.bin"
set "APP_OFFSET=0x00008000"

if not exist "%BOOT_BIN%" (
  echo [ERROR] Boot BIN not found: "%BOOT_BIN%"
  exit /b 1
)

if not exist "%APP_BIN%" (
  echo [ERROR] App BIN not found: "%APP_BIN%"
  echo [ERROR] Please rebuild the Keil project first.
  exit /b 1
)

set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL_EXE%" (
  echo [ERROR] PowerShell was not found: "%POWERSHELL_EXE%"
  exit /b 1
)

echo [INFO] Boot  BIN: "%BOOT_BIN%"
echo [INFO] App   BIN: "%APP_BIN%"
echo [INFO] Merge BIN: "%MERGED_BIN%"
echo [INFO] App offset: %APP_OFFSET%

"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -Command ^
  "$bootPath = [System.IO.Path]::GetFullPath($env:BOOT_BIN);" ^
  "$appPath = [System.IO.Path]::GetFullPath($env:APP_BIN);" ^
  "$mergedPath = [System.IO.Path]::GetFullPath($env:MERGED_BIN);" ^
  "$appOffset = [Convert]::ToInt32($env:APP_OFFSET, 16);" ^
  "$boot = [System.IO.File]::ReadAllBytes($bootPath);" ^
  "$app = [System.IO.File]::ReadAllBytes($appPath);" ^
  "if ($boot.Length -gt $appOffset) { throw ('boot.bin is too large: 0x{0:X} bytes exceeds app offset 0x{1:X}.' -f $boot.Length, $appOffset) }" ^
  "$mergedSize = [Math]::Max($boot.Length, $appOffset + $app.Length);" ^
  "$merged = New-Object byte[] $mergedSize;" ^
  "for ($i = 0; $i -lt $merged.Length; $i++) { $merged[$i] = 0xFF }" ^
  "[Array]::Copy($boot, 0, $merged, 0, $boot.Length);" ^
  "[Array]::Copy($app, 0, $merged, $appOffset, $app.Length);" ^
  "[System.IO.File]::WriteAllBytes($mergedPath, $merged);" ^
  "Write-Host ('[INFO] Merged image size: {0} bytes' -f $merged.Length)"
if errorlevel 1 (
  echo [ERROR] Failed to build merged flash image.
  exit /b 1
)
echo [INFO] Merge completed successfully.
exit /b 0
