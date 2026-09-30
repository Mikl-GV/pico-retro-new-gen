#!/bin/bash
# ms1504_rename.sh — переименование глобальных символов набора мс1504 в m15_*.
#
# Зачем: снять коллизии с другими ядрами под --allow-multiple-definition
# (пример: `timing` есть и в ядре BK — линкер брал первое определение, и
# вызов из fake86 попадал в BK-функцию с указателем → Data Abort 0x15).
#
# Идемпотентно и безопасно для ИНКРЕМЕНТАЛЬНОЙ сборки:
#   * канонический набор = голая логическая схема имён: из ВСЕХ определённых
#     глобалов набора, у m15_* префикс снимается (m15_RAM → RAM);
#   * для каждого объекта переименовываются только ГОЛЫЕ вхождения (def или
#     ref) имён из канона → m15_<имя>; уже переименованные и внешние
#     (printf/memset: в каноне их нет) не трогаются.
#
# KEEP — символы, нужные внешнему миру (диспетчер меню, вшитые дампы BIOS).
# Usage: ms1504_rename.sh <build/ms1504_*.o...>
set -u

KEEP="emu_run_ms1504 ms1504_bios_pk300 ms1504_bios_pk300_len ms1504_hdd_bios ms1504_hdd_bios_len"

DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

# канон: голая логическая схема
CANON=""
for n in $DEFS; do
  case "$n" in
    m15_*) x=${n#m15_};;
    *)     x=$n;;
  esac
  case " $KEEP " in *" $x "*) continue;; esac
  case "$CANON " in *" $x "*) continue;; esac
  CANON="$CANON $x"
done

for o in "$@"; do
  [ -f "$o" ] || continue
  ARGS=""
  for x in $CANON; do
    # голое вхождение x (определение ИЛИ ссылка) есть в этом объекте?
    if arm-none-eabi-nm "$o" 2>/dev/null | awk '{print $NF}' | grep -qx "$x"; then
      ARGS="$ARGS --redefine-sym $x=m15_$x"
    fi
  done
  if [ -n "$ARGS" ]; then
    arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
  fi
done
exit 0