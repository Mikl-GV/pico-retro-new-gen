#!/bin/bash
# build.sh — обёртка над официальной сборкой через Makefile (r500).
# Использование: ./build.sh [clean|sd|fel]   (без аргумента = make -j)
#
# ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ — Makefile: все рецепты сборки (состав файлов,
# флаги, rename-скрипты, линковка) живут ТОЛЬКО там. Этот скрипт делегирует
# в make и автоматически остаётся актуальным.
#
# ПРАВИЛО АКТУАЛЬНОСТИ: НЕ дублировать здесь списки файлов и команды сборки.
# Если в Makefile изменился состав прошивки — build.sh менять не нужно.
set -euo pipefail

TOP="$(cd "$(dirname "$0")" && pwd)"
cd "$TOP"

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

case "${1:-}" in
    "" )
        make -j"$JOBS"
        ;;
    clean )
        make clean
        ;;
    sd )
        make sd
        ;;
    fel )
        make fel
        ;;
    * )
        echo "build.sh: неизвестная цель '$1' (допустимо: clean, sd, fel, или без аргумента)" >&2
        exit 1
        ;;
esac