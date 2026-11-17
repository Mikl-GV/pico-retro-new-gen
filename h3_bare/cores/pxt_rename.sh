#!/bin/bash
# pxt_rename.sh — переименование глобальных символов набора pxt (ядро PC/XT-286,
# порт emulator-refs/pico-286) в pxt_*.
#
# Зачем: снять коллизии с другими C-ядрами под --allow-multiple-definition.
# Ядро pico-286 — чистый C с «голыми» именами, которые заведомо пересекаются
# с Fake86/MS1504 и прочими: RAM, VIDEORAM, videomode, exec86, reset86,
# portin/portout, running и т.п. --allow-multiple-definition брал бы одно тело
# на оба символа → вызовы уходили в чужое ядро → Data Abort / мусор.
#
# Идемпотентно и безопасно для ИНКРЕМЕНТАЛЬНОЙ сборки:
#   * канонический набор = голая логическая схема имён: из ВСЕХ определённых
#     глобалов набора, у pxt_* префикс снимается (pxt_RAM → RAM);
#   * для каждого объекта переименовываются только ГОЛЫЕ вхождения (def ИЛИ
#     ref) имён из канона → pxt_<имя>; уже переименованные и внешние
#     (printf/memset: в каноне их нет) не трогаются.
#
# ВАЖНО: список объектов включает ядро И host-слой (pxt_host.c). Host внутри
# ссылается на символы ядра голыми именами (RAM, exec86, ...) — после ренеима
# они автоматически становятся pxt_* и совпадают с переименованным ядром.
#
# KEEP — символы, нужные внешнему миру (диспетчер меню / rom_browser). Точка
# входа эмулятора emu_run_pxt определена в host-слое и не должна получить
# префикс, т.к. её вызывает рom_browser через объявление в emu.h.
# Usage: pxt_rename.sh <build/pxt_*.o...>
set -u

KEEP="emu_run_pxt"

DEFS=$(for o in "$@"; do
  [ -f "$o" ] || continue
  arm-none-eabi-nm "$o" 2>/dev/null | awk '$2 ~ /^[TDBWV]$/ {print $NF}'
done | sort -u)

# канон: голая логическая схема
CANON=""
for n in $DEFS; do
  case "$n" in
    pxt_*) x=${n#pxt_};;
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
      ARGS="$ARGS --redefine-sym $x=pxt_$x"
    fi
  done
  if [ -n "$ARGS" ]; then
    arm-none-eabi-objcopy $ARGS "$o" "$o.tmp" && mv "$o.tmp" "$o"
  fi
done
exit 0