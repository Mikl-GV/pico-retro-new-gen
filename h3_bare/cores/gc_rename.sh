#!/bin/bash
# Переименовать в gc-объекте все символы, которые определены в blargg-коде
# Lynx (build/lynx_blip_buffer.o + build/lynx_blip_stereo.o). У Lynx и
# Gearcoleco общая библиотека blip (Blip_Buffer/Stereo_Buffer/Multi_Buffer/
# Effects_Buffer/...), что даёт multiple definition при линковке.
# Обходим это, добавляя суффикс _gc ко всем таким символам в gc-объектах
# (objcopy переименовывает и определения, и ссылки внутри объекта).
#
# База берётся из build-объектов Lynx: если их ещё нет (первый прогон до
# их сборки), скрипт ничего не делает — при финальной линковке конфликт
# будет виден, но обычно Lynx собирается параллельно и база есть.
IN=$1
if [ ! -f "$IN" ]; then exit 0; fi
BUILD_DIR=$(dirname "$IN")

# Все глобальные _Z-символы, определённые в Lynx-версиях blargg
LINX_SYMS=""
for L in "$BUILD_DIR/lynx_blip_buffer.o" "$BUILD_DIR/lynx_blip_stereo.o"; do
  [ -f "$L" ] || continue
  LINX_SYMS="$LINX_SYMS $(arm-none-eabi-nm "$L" 2>/dev/null | awk '$2 ~ /^[TDBW]$/ {print $3}' | grep '^_Z' | sort -u)"
done
[ -z "$LINX_SYMS" ] && exit 0

# Символы, реально присутствующие в нашем объекте
MY_SYMS=$(arm-none-eabi-nm "$IN" 2>/dev/null | awk '{print $3}' | sort -u)

ARGS=""
for sym in $LINX_SYMS; do
  if printf '%s\n' "$MY_SYMS" | grep -Fqx "$sym"; then
    ARGS="$ARGS --redefine-sym $sym=${sym}_gc"
  fi
done

if [ -n "$ARGS" ]; then
  arm-none-eabi-objcopy $ARGS "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
fi
exit 0