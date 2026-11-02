#!/bin/bash
# vice_rename.sh — префикс vice_ для всех глобальных символов объектов набора
# VICE (ядро C64/C128 + libretro-обвязка + libretro-common + deps zlib/7zip).
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: main, log, lib, file_path, filestream_*, crc32, retro_*,
# sound, machine, sysfile, keyboard, joystick, ...).
# Идемпотентен: канон по всем объектам (префикс vice_ снимается), в объекте
# переименовываются только голые вхождения (определения И ссылки).
#
# r974: набор 584 объекта, CANON ~несколько тысяч символов — objcopy с ВСЕМИ
# --redefine-sym в одной команде упирается в лимит аргументов Windows
# ("Argument list too long"), и часть объектов остаётся непереименованной
# (голые cart_*/main/log при линковке). Поэтому ARGS бьются на порции по
# 100 символов и objcopy применяется последовательно к каждому объекту.
#
# KEEP — символы, нужные внешнему миру (точка входа host-слоя: retro_* ->
# vice_retro_* — префикс тоже даёт уникальность, KEEP пуст).
# Usage: vice_rename.sh <build/vice*_*.o...>
set -u

KEEP=""

# канон: голая логическая схема имён (с префикса vice_ снятого) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    vice_*) x=${n#vice_};;   # уже с префиксом: известно как vice_<голое>
    *)      x=$n;;
  esac
  case " $KEEP " in *" $x "*) continue;; esac
  case "$CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

BATCH=100   # --redefine-sym на один вызов objcopy (лимит командной строки)

for o in "$@"; do
  [ -f "$o" ] || continue
  OBSYM=$(arm-none-eabi-nm "$o" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' ')
  [ -z "$OBSYM" ] && continue

  ARGS=""
  for x in $CANON; do
    case " $OBSYM " in
      *" $x "*) ARGS="$ARGS --redefine-sym $x=vice_$x";;
    esac
  done
  [ -z "$ARGS" ] && continue

  # применяем порциями: каждый objcopy переписывает -o.o.tmp -> -o.o
  tmp="$o.rn"
  cp -f "$o" "$tmp"
  n=0
  chunk=""
  for a in $ARGS; do
    chunk="$chunk $a"
    n=$((n+1))
    if [ $((n % (BATCH*2))) -eq 0 ]; then
      arm-none-eabi-objcopy $chunk "$tmp" "$tmp" && chunk=""
    fi
  done
  if [ -n "$chunk" ]; then
    arm-none-eabi-objcopy $chunk "$tmp" "$tmp"
  fi
  mv -f "$tmp" "$o"
done
exit 0