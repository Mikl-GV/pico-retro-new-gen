#!/bin/bash
# frodo_rename.sh — префикс frodo_ для всех глобальных символов объектов набора
# Frodo (ядро C64 libretro-порта r-type/frodo-libretro, V4_2). Замена VICE
# (r1284): ядро мёртвое на H3 (тёмный/пустой кадр x64sc), владелец: путь
# закрыт, VICE удалить, Frodo встроить.
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: retro_*, main, log, C64 и пр.).
# Идемпотентен: канон строится по всем объектам (префикс frodo_ снимается),
# в объекте переименовываются только голые вхождения (определения И ссылки).
#
# Аналог uae4all_rename.sh/puae_rename.sh: ARGS бьются на порции по 100
# символов — objcopy с полным списком упирается в лимит командной строки
# Windows. Host-слой (frodo_host.o, uae4all_stubs.o, fs_layer.o, libc_min.o)
# НЕ входит в набор — держит внешние имена (emu_run_frodo, fs_register) и
# зовёт frodo_retro_*.
# Usage: frodo_rename.sh <build/fro_*.o...>
set -u

# Имена, которые НЕ переименовываем (остаются голыми; определения и ссылки
# в наборе разрешаются через глобальный пул проекта):
# - malloc/calloc/realloc/free: глобальный gb-пул (gameboy_stubs.c).
# - usleep/select/statfs/mkdir/rmdir/strerror/sigaction/gettimeofday: внешние
#   POSIX из uae4all_stubs.c/fs_layer.c/libc_min.c — их Frodo только вызывает;
#   в канон они не попадают (определений в наборе нет), поэтому голыми и
#   остаются, но KEEP страхует от случайного определения в одном из объектов.
KEEP="malloc calloc realloc free usleep select statfs mkdir rmdir strerror sigaction gettimeofday"

# канон: голая логическая схема имён (с префикса frodo_ снятого) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    frodo_*) x=${n#frodo_};;   # уже с префиксом: известно как frodo_<голое>
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
      *" $x "*) ARGS="$ARGS --redefine-sym $x=frodo_$x";;
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