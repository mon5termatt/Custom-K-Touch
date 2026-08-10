@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Rebuild Klipper Touch and flash per-partition over USB-serial (preserves NVS).
rem
rem Usage:
rem   scripts\rebuild-flash.bat [PORT] [BAUD] [IDF_VERSION]
rem   scripts\rebuild-flash.bat COM4
rem   scripts\rebuild-flash.bat COM4 460800 v5.3.1
rem
rem Defaults: PORT=COM4  BAUD=460800  IDF_VERSION=v5.3.1
rem Prefers Espressif EIM (`eim run`); falls back to IDF_PATH\export.bat.

set "FW=%~dp0.."
cd /d "%FW%" || exit /b 1

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM4"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=460800"
set "IDF_VER=%~3"
if "%IDF_VER%"=="" set "IDF_VER=v5.3.1"

echo.
echo === Building klipper-touch (%IDF_VER%) ===
echo.

set "BUILD_RC=1"
where eim >nul 2>&1
if not errorlevel 1 (
  eim run "idf.py build" %IDF_VER%
  set "BUILD_RC=!ERRORLEVEL!"
) else if defined IDF_PATH (
  if not exist "%IDF_PATH%\export.bat" (
    echo IDF_PATH is set but export.bat was not found: "%IDF_PATH%\export.bat"
    exit /b 1
  )
  call "%IDF_PATH%\export.bat"
  if errorlevel 1 goto :build_fail
  idf.py build
  set "BUILD_RC=!ERRORLEVEL!"
) else (
  echo Neither "eim" nor IDF_PATH is available.
  echo Install ESP-IDF via EIM, or open an ESP-IDF PowerShell/CMD and re-run.
  exit /b 1
)

if not "!BUILD_RC!"=="0" goto :build_fail
if not exist "build\klipper-touch.elf" goto :build_fail
if not exist "build\klipper-touch.bin" goto :build_fail
rem Confirm main lib was rebuilt this run (ninja updates it when sources compile).
if not exist "build\esp-idf\main\libmain.a" goto :build_fail

echo.
echo === Flashing %PORT% @ %BAUD% (NVS preserved) ===
echo.

python -m esptool --chip esp32s3 -p %PORT% -b %BAUD% --before default_reset --after hard_reset ^
  write_flash --flash_mode dout --flash_size 16MB --flash_freq 80m ^
  0x0     build\bootloader\bootloader.bin ^
  0x8000  build\partition_table\partition-table.bin ^
  0xe000  build\ota_data_initial.bin ^
  0x10000 build\klipper-touch.bin
if errorlevel 1 (
  echo Flash failed.
  exit /b 1
)

echo.
echo Done.
exit /b 0

:build_fail
echo Build failed.
exit /b 1
