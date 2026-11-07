#!/bin/bash
# mgba_rename.sh — переименовать конфликтующие символы в mgba_*.o для
# единой линковки с --allow-multiple-definition (аналогично ngp_rename.sh).
#
# r1132: mGBA-core тянет mgba_util_{crc32,string}.o, которые определяют
# глобальные crc32/strlcpy; те же имена уже есть в других ядрах (fceumm и
# т.п.). --allow-multiple-definition выбрал бы ОДНО тело на оба символы, что
# сломало бы вызывающий код. Переименовываем во ВСЕХ mgba-объектах в
# mgba_crc32/mgba_strlcpy (и определения, и ссылки).
#
# Принимает ЛЮБОЕ число файлов (make передаёт список mgba_*.o).
for IN in "$@"; do
  [ -f "$IN" ] || continue

  if arm-none-eabi-nm "$IN" 2>/dev/null | grep -qE ' (T|U) (crc32|strlcpy)$'; then
    arm-none-eabi-objcopy \
      --redefine-sym crc32=mgba_crc32 \
      --redefine-sym strlcpy=mgba_strlcpy \
      "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
  fi
done
exit 0