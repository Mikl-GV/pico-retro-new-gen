#!/bin/bash
# cw_rename.sh — переименование глобальных символов набора cw (ядро
# Atari ST, Castaway) в cw_*.
#
# Зачем: снять коллизии с другими C-ядрами (Init, VBlank, IOTimer, MemInit,
# FDCInit, IOInit, CPURun, IkbdKeyPress... — «голые» имена пересекаются с
# MSX/PXT/Fuse и др.). Без префикса линковщик взял бы одно тело на оба
# символа → вызовы уходили в чужое ядро → Data Abort/мусор.
#
# Идемпотентно и безопасно для ИНКРЕМЕНТАЛЬНОЙ сборки:
#   * канонический набор = голая логическая схема имён: из ВСЕХ определённых
#     глобалов набора, у cw_* префикс снимается (cw_Init → Init);
#   * для каждого объекта переименовываются только ГОЛЫЕ вхождения (def ИЛИ
#     ref) имён из канона → cw_<имя>; уже переименованные и внешние
#     (printf/memset: в каноне их нет) не трогаются.
#
# KEEP — символы, нужные внешнему миру (диспетчер меню / rom_browser).
# Точка входа cw_run определена в host-слое и не должна получить префикс.
#
# Usage: cw_rename.sh <build/cw_*.o...>
set -eu

KEEP="cw_run emu_run_atari_st_cw"

SYMS_OF() { arm-none-eabi-nm "$1" 2>/dev/null | awk '{print $NF}' | tr '\n' ' '; }

TMPDIR_REN="${TMPDIR:-/tmp}/cw_rename.$$"
mkdir -p "$TMPDIR_REN"
trap 'rm -rf "$TMPDIR_REN"' EXIT

CANON=""
for o in "$@"; do
  [ -f "$o" ] || continue
  for n in $(arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'); do
    case "$n" in
      cw_*) x=${n#cw_};;
      *)    x=$n;;
    esac
    case " $KEEP " in *" $x "*) continue;; esac
    case "$CANON " in *" $x "*) continue;; esac
    CANON="$CANON $x"
  done
done

for o in "$@"; do
  [ -f "$o" ] || continue
  SYMS=" $(SYMS_OF "$o") "
  REDEF="$TMPDIR_REN/$(basename "$o").txt"
  : > "$REDEF"
  for x in $CANON; do
    case " $SYMS " in *" $x "*) printf '%s cw_%s\n' "$x" "$x" >> "$REDEF";; esac
  done
  [ -s "$REDEF" ] || continue
  arm-none-eabi-objcopy --redefine-syms="$REDEF" "$o" "$o.tmp"
  mv "$o.tmp" "$o"
done
exit 0
