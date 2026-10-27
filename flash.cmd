@echo off
rem ============================================================
rem  FEL flash for Orange Pi Lite (H3): U-Boot + h3_bare.bin
rem  Board in FEL mode: hold FEL button while powering on.
rem  Re-runnable: just run again to reflash.
rem
rem  Overridable env vars:
rem    FEL_TOOL  path\sunxi-fel.exe
rem    UBOOT_BIN path\u-boot-sunxi-with-spl.bin
rem    FW_BIN    path\h3_bare.bin
rem ============================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
if not defined FEL_TOOL set "FEL_TOOL=%ROOT%..\Bare_linux\tools\fel\bin\sunxi-fel.exe"
if not defined UBOOT_BIN set "UBOOT_BIN=%ROOT%..\Bare_linux\tools\u-boot\u-boot-sunxi-with-spl.bin"
if not defined FW_BIN    set "FW_BIN=%ROOT%h3_bare.bin"

if not exist "%FEL_TOOL%" goto miss
if not exist "%UBOOT_BIN%" goto miss
if not exist "%FW_BIN%" goto miss

echo == 1. Looking for board in FEL mode...
"%FEL_TOOL%" -l >nul 2>&1
if errorlevel 1 goto nofel
echo    Board found.

echo == 2. Flashing U-Boot + h3_bare.bin...
"%FEL_TOOL%" uboot "%UBOOT_BIN%" write 0x40000000 "%FW_BIN%"
if errorlevel 1 goto fail

echo.
echo == DONE. U-Boot booted, h3_bare.bin running. ==
echo    UART log: 115200 8N1 (open terminal BEFORE power-on).
echo.
pause
exit /b 0

:miss
echo [ERROR] Missing file (set FEL_TOOL / UBOOT_BIN / FW_BIN):
echo   "%FEL_TOOL%"
echo   "%UBOOT_BIN%"
echo   "%FW_BIN%"
goto fail

:nofel
echo [ERROR] No board in FEL mode.
echo   Hold FEL button while powering on; check USB cable.
goto fail

:fail
echo.
echo [FAIL] Flashing not done.
pause
exit /b 1