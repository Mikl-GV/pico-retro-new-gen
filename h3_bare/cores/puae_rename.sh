#!/bin/bash
# puae_rename.sh — префикс puae_ для всех глобальных символов объектов набора
# PUAE (ядро Amiga + libretro-обвязка + retrodep + zlib).
# Снимает коллизии с другими ядрами под --allow-multiple-definition
# (generic-имена: main, log, gui, sound, parser, crc32, retro_*, util_*,
# string_list, file_path, ...). Идемпотентен: канон по всем объектам
# (префикс puae_ снимается), в объекте переименовываются только голые
# вхождения (определения И ссылки).
#
# Аналог vice_rename.sh (r974): ARGS бьются на порции по 100 символов —
# objcopy с полным списком символов набора упирается в лимит Windows.
# Host-слой (puae_host.o) НЕ входит в набор — держит внешние имена
# (emu_run_amiga, puae_fs_register) и зовёт puae_retro_*.
# Usage: puae_rename.sh <build/pz_*.o...>
set -u

KEEP=""
# malloc/calloc/realloc/free в наборе — только ССЫЛКИ (определения живут в
# gameboy_stubs.c, пул .gb_heap). Их переименование в puae_malloc/... уводит
# PUAE на собственный аллокатор (puae_host.c, sbrk-зона) и исключает
# пересечение с gb-пулом (стенд: abort на 0x4A800000 — calloc из .gb_heap).

# канон: голая логическая схема имён (с префикса puae_ снятого) из ВСЕХ объектов
DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

CANON=""
for n in $DEFS; do
  case "$n" in
    puae_*) x=${n#puae_};;   # уже с префиксом: известно как puae_<голое>
    *)      x=$n;;
  esac
  case " $KEEP " in *" $x "*) continue;; esac
  case "$CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

# Явные ссылки, переопределяемые в ЛЮБОМ объекте набора (даже если в наборе
# нет их определения): malloc/calloc/realloc/free определены глобально в
# gameboy_stubs.c (пул .gb_heap, 0x4A700000). PUAE ходит в них через
# xcalloc/mapped_malloc → на стенде abort на 0x4A800000 (1 МБ от начала
# пула: bump-адрес = бюджет эмулятора). Переименование уводит PUAE на
# собственный аллокатор puae_* (puae_host.c, sbrk-зона) — устраняет
# пересечение аллокаторов без правки глобального malloc.
for x in malloc calloc realloc free memalign; do
  case " $CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

# strdup/strndup в PUAE ходят в глобальный newlib-strdup (x_strdup/misc.c:754,
# sysdeps.h _tcsdup), который внутри аллоцирует через malloc → тот же gb-пул.
# Переименовываем ссылки на puae_strdup → определение в puae_host.c (там же
# puae_malloc). strnoteq_exit — чисто для symtab-спокойствия.
for x in strdup strndup; do
  case " $CANON " in *" $x "*) continue;; esac
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
      *" $x "*) ARGS="$ARGS --redefine-sym $x=puae_$x";;
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