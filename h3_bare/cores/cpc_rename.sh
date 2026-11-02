#!/bin/bash
# cpc_rename.sh — префикс cpc_ для всех глобальных символов объектов набора
# cap32 (ядро Amstrad CPC + libretro-обвязка + libretro-common).
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: cap32, video, audio, draw_*, retro_*, crc32, tape, z80_*, ...).
# Идемпотентен: канон по всем объектам (префикс cpc_ снимается из имени в KEEP).
# В объекте переименовываются только голые вхождения (определения И ссылки)
# тех символов, которые есть в KEEP-каноне.
# KEEP — символы, нужные внешнему миру (точка входа host-слоя: retro_* -> cpc_retro_*).
# Usage: cpc_rename.sh <build/cpc*_*.o...>
set -u

# KEEP — символы, НЕ переименовываемые на префикс cpc_ (пусто: все глобальные
# имена ядра/обвязки, включая rf*/filestream_*, получают префикс cpc_ —
# реализацию даёт cpc_fs.c с теми же cpc_* именами). Это снимает коллизии с
# gpgx_missing.c/pce_stubs.c (rfopen/rfread/... и *_vfs_init определены там же).
KEEP=""

# канон: голая логическая схема имён (с префикса cpc_ снятого) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    cpc_*) x=${n#cpc_};;   # уже с префиксом: известно как cpc_<голое>
    *)     x=$n;;
  esac
  case " $KEEP " in *" $x "*) continue;; esac
  case "$CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

for o in "$@"; do
  [ -f "$o" ] || continue
  OBSYM=$(arm-none-eabi-nm "$o" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' ')
  [ -z "$OBSYM" ] && continue
  ARGS=""
  for x in $CANON; do
    case " $OBSYM " in
      *" $x "*) ARGS="$ARGS --redefine-sym $x=cpc_$x";;
    esac
  done
  if [ -n "$ARGS" ]; then
    arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
  fi
done
exit 0