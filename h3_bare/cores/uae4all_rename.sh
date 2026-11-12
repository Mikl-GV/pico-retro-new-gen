#!/bin/bash
# uae4all_rename.sh — префикс u4a_ для всех глобальных символов объектов набора
# uae4all (ядро Amiga UAE4All, libretro-порт Chips). Замена PUAE (r1243).
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: memory, custom, disk, sound, gui, xmalloc, retro_*, ...).
# Идемпотентен: канон строится по всем объектам (префикс u4a_ снимается),
# в объекте переименовываются только голые вхождения (определения И ссылки).
#
# Аналог puae_rename.sh (r1192): ARGS бьются на порции по 100 символов —
# objcopy с полным списком символов набора упирается в лимит Windows.
# Host-слой (uae4all_host.o) НЕ входит в набор — держит внешние имена
# (emu_run_uae4all, fs_register) и зовёт u4a_*.
# Usage: uae4all_rename.sh <build/u4a_*.o...>
set -u

# Имена, которые НЕ переименовываем (остаются голыми; определения и ссылки
# в наборе разрешаются через глобальный пул проекта):
# - malloc/calloc/realloc/free: глобальный gb-пул (gameboy_stubs.c) — uae4all
#   ходит в него штатно (без abort-кейса PUAE: у uae4all нет xcalloc-гонки).
KEEP="malloc calloc realloc free"

# канон: голая логическая схема имён (с префикса u4a_ снятого) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    u4a_*) x=${n#u4a_};;   # уже с префиксом: известно как u4a_<голое>
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
      *" $x "*) ARGS="$ARGS --redefine-sym $x=u4a_$x";;
    esac
  done
  [ -z "$ARGS" ] && continue

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