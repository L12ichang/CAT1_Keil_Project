@echo off
if "%~1"=="" (
  echo [ERROR] Missing Keil output name.
  exit /b 1
)
python "%~dp0..\..\tools\keil_hex_to_app_bin.py" --hex "%~dp0%~1.hex" --bin "%~dp0%~1.bin"
