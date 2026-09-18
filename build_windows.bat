@echo off
REM ============================================================
REM  pico-retro-new-gen — сборка ПРОШИВКИ на Windows
REM  Просто запусти этот файл ДВОЙНЫМ ЩЕЛЧКОМ (или из консоли).
REM  На выходе: h3_bare.bin в корне проекта.
REM ============================================================
setlocal
cd /d "%~dp0"

echo.
echo  ============================================
echo   pico-retro-new-gen: сборка (Windows)
echo  ============================================
echo.

REM ---- ищем PowerShell (есть в любой Windows 10/11) ----
where powershell >nul 2>&1
if errorlevel 1 (
    echo [ОШИБКА] PowerShell не найден. Нужна Windows 10/11.
    pause
    exit /b 1
)

REM ---- запускаем сборку ----
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
set RC=%errorlevel%

echo.
if %RC%==0 (
    echo  ============================================
    echo   ГОТОВО. Прошивка: %~dp0h3_bare.bin
    echo   Запиши на SD-карту (см. docs/BUILD.md):
    echo     sudo cp h3_bare.bin /mnt/
    echo  ============================================
) else (
    echo  ============================================
    echo   СБОРКА ЗАВЕРШИЛАСЬ С ОШИБКОЙ (код %RC%).
    echo   Скопируй текст выше — он нужен для диагностики.
    echo  ============================================
)
echo.
pause
exit /b %RC%