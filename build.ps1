# build.ps1 — обёртка над официальной сборкой через Makefile (r500).
# Использование:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1          # make -j
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Target clean|sd|fel
#
# ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ — Makefile: все рецепты сборки (состав файлов,
# флаги, rename-скрипты, линковка) живут ТОЛЬКО там. Скрипт делегирует в
# make и автоматически остаётся актуальным.
#
# ПРАВИЛО АКТУАЛЬНОСТИ: НЕ дублировать здесь списки файлов и команды сборки.
# Для Windows требуется make (MSYS2/MinGW) и ARM-тулчейн в PATH.
param([string]$Target = "")

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Push-Location $Root
try {
    $cpu = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" }
    switch ($Target) {
        "clean" { & make clean }
        "sd"    { & make sd }
        "fel"   { & make fel }
        default { & make -j $cpu }
    }
    if ($LASTEXITCODE -ne 0) { throw "make завершился с ошибкой: $LASTEXITCODE" }
} finally {
    Pop-Location
}