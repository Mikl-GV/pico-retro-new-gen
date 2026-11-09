#!/bin/bash
# picodrive_rename.sh — переименовать конфликтующие символы в pico-объектах
# для единой линковки с --allow-multiple-definition.
#
# PicoDrive (ядро 32X) заводит глобалы, пересекающиеся с уже линкуемыми
# ядрами проекта (gpgx = Mega Drive/SMS, cps1 = FBNeo Musashi, msx = fMSX):
#   - m68k* / m68ki* / m68k_op_*  — Musashi (совпадает с gpgx m68k и cps1 c1m68k)
#   - Pico* / Pico32x* / p32x*    — API ядра PicoDrive
#   - z80* / CZ80 / Cz80          — Z80-эмулятор
#   - sh2* / sh2s*                — SH2 (32X)
#   - YM2612* / ym2612* / YM2413* / SN76496* / mix_* / Psnd* / opll / OPLL*
#   - Sek* / sek*                 — 68000-обвязка PicoDrive
#   - cdd_* / cdc_* / pcm_*       — карман CD (конфликт с gpgx_cd_hw_*)
#   - gfx_* / s68k_* / mcd_*      — CD-видео/суб-68000
#   - wram_* / DmaSlowCell / memcpy16bswap / memset32 — CD-вспомогательные
#   - gz* / inflate* / adler32 / crc32 / zlib* / compress* / deflate* / uncompress
#                                   — zlib/unzip (crc32 vs gpgx_missing,
#                                   inflate vs fz_zlib_inflate, gz_FS)
#   - openzip / readzip / seekcompresszip / closezip — unzip
#   - chd_* / cue_* / cdparse_*   — парсеры CD-образов
# Чтобы внешние ссылки не привязались к чужой реализации под
# --allow-multiple-definition, префиксуем конфликтующие имена в pd-*.
# Префикс применяется СОГЛАСОВАННО: и к определениям, и к ссылкам внутри
# каждого объекта (objcopy --redefine-sym), а host 32x_host.c использует
# переименованные имена (pd_PicoInit и т.п.).
#
# Windows/MSYS: как cps1_rename.sh — аргументы через response-файл @file,
# чтобы не упереться в лимит командной строки (~32K) на Windows.
IN=$1
[ -f "$IN" ] || exit 0

REFLIST="$IN.rsp"
: > "$REFLIST"
# objcopy --redefine-sym правит и определения, и ссылки в объекте. Для nm:
# последний столбец ($NF) — всегда имя символа, и для определений (T/D/B/R),
# и для неопределённых ссылок (U/w/v — там тип в ПЕРВОМ поле, числа нет).
# Фильтр по префиксу на $NF без разбора типа (как cps1_rename.sh): так
# переименовываются согласованно и определения, и межобъектные ссылки внутри
# каждого pico-объекта (включая host). Применяется к каждому объекту.
for sym in $(arm-none-eabi-nm "$IN" 2>/dev/null | awk '{print $NF}' | \
    grep -E '^(m68k|m68ki|Pico|Pico32x|p32x|z80|CZ80|Cz80|sh2|sh2s|SN76496|YM2612|ym2612|YM2413|mix_|Psnd|psnd|opll|OPLL|[Ss]ek|cdd_|cdc_|pcm_|gfx_|gfx|s68k_|mcd_|wram_|DmaSlowCell|memcpy16bswap|memset32|openzip|readzip|seekcompresszip|closezip|chd_|cue_|cdparse_|eeprom_spi|ssp16|ssp1601|svp)' | sort -u); do
  printf '%s\n' "--redefine-sym=$sym=pd_$sym" >> "$REFLIST"
done

if [ -s "$REFLIST" ]; then
  arm-none-eabi-objcopy @"$REFLIST" "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
fi
rm -f "$REFLIST"
exit 0