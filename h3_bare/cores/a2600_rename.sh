#!/bin/bash
# a2600_rename.sh — переименовать C++-символы ядра Stella (a26-объекты),
# конфликтующие с другими ядрами проекта, суффиксом _a26.
#
# Конфликт: Stella (emucore/Cart.hxx) и Gearcoleco (gc_Cartridge.o) оба
# определяют класс Cartridge; то же для System/Settings/Console/Sound/TIA/
# M6502/M6532/Serializer/Random/MD5/Properties/Switches/Paddles/EventHandler.
# Дубли _ZN...* ловит linkcheck, а --allow-multiple-definition склеивает
# РАЗНЫЕ тела (порча кучи).
#
# Карта переименования СОХРАНЯЕТСЯ в build/a2600_syms.map и применяется к
# КАЖДОМУ прогону ко всем a26-объектам. Иначе после первого прогона
# определения уже с _a26, список становится пустым — и перекомпилированный
# a2600_host.o остаётся с исходными ссылками (undefined reference,
# поймано сборкой r1526). Карта строится один раз по свежим объектам;
# `make clean` удаляет build/ и карту пересобирает.
#
# Вызов: a2600_rename.sh <build_dir> <obj1> <obj2> ...
set -u
BUILD_DIR="${1:?build dir}"; shift
[ "$#" -gt 0 ] || exit 0

command -v arm-none-eabi-objcopy >/dev/null 2>&1 || \
  export PATH="$PATH:/c/ARM/gcc-arm-none-eabi-15.2.1/bin"

MAP="$BUILD_DIR/a2600_syms.map"

if [ ! -s "$MAP" ]; then
  TMP="$MAP.tmp"
  for obj in "$@"; do
    [ -f "$obj" ] || continue
    arm-none-eabi-nm "$obj" 2>/dev/null | \
      awk '$2 ~ /^[TDBWRV]$/ && $3 ~ /^_Z/ { print $3 }'
  done | sort -u | grep -v '_a26$' | awk '{ print $1, $1 "_a26" }' > "$TMP"
  mv "$TMP" "$MAP"
fi

[ -s "$MAP" ] || exit 0

for obj in "$@"; do
  [ -f "$obj" ] || continue
  arm-none-eabi-objcopy --redefine-syms="$MAP" "$obj" "$obj.rn" && mv "$obj.rn" "$obj"
done
exit 0
