@echo off
REM  Build h3_bare.bin for Orange Pi Lite (Windows).
REM  DOUBLE-CLICK this file to build -- that's all.
REM  Requires: arm-none-eabi-gcc (see README.md).
cd /d "%~dp0"
echo.
echo ============================================
echo  Building pico-retro-new-gen (Windows)
echo ============================================
echo.
where powershell >nul 2>&1 || ( echo ERROR: PowerShell not found. && pause && exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 (
    echo.
    echo BUILD FAILED. Copy the output above for diagnostics.
    pause
    exit /b 1
)
echo.
echo ============================================
echo  DONE. Firmware ready: %~dp0h3_bare.bin
echo  Copy to SD card boot partition.
echo ============================================
echo.
pause