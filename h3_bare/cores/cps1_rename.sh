#!/bin/bash
# cps1_rename.sh — переименовать конфликтующие символы в cps1-объектах
# для единой линковки с --allow-multiple-definition.
#
# Что и почему:
#  - m68k* / m68ki* (Musashi из FBNeo) полностью совпадают по именам с
#    m68k из Genesis Plus GX (gpgx) → extern-ссылки могли привязаться к
#    чужой реализации (экспириенс BK/PCE). Переименовываем в c1m68k*.
#  - YM2612* (fm.c, компилируется как C) совпадает с YM2612* из gpgx sound.
#    Внутри cps1 не используется, но определение не должно «съедать» чужое
#    под --allow-multiple-definition → переименовываем в c1YM2612*.
#  - YM2413* (ym2413.c, как C) совпадает с YM2413* из gpgx sound (r725):
#    gpgx_sound_sound.o ссылается на YM2413*, а c1sc_ym2413.o их определяет —
#    allow-multiple мог отдать FBNeo-тело GPGX-ссылкам. Переименовываем в
#    c1YM2413* (согласованно: определения c1sc_ym2413 и ссылки c1s_burn_ym2413
#    внутри набора переименуются вместе).
#
# ВАЖНО: применяется к КАЖДОМУ cps1-объекту после компиляции (соответственно
# переименовываются и определения, и ссылки внутри объекта). all m68k/YM2612/
# YM2413 токены перечисляются динамически через nm (как gc_rename.sh по токенам).
#
# Windows/MSYS: сгенерированный m68kops.c даёт ~2000 токенов, и прямая команда
# objcopy --redefine-sym ... каждой строкой превышает лимит командной строки
# Windows (~32K) → «Argument list too long». Передаём аргументы через response-
# файл (@file), который binutils читает целиком, без ограничения по длине.
IN=$1
[ -f "$IN" ] || exit 0

REFLIST="$IN.rsp"
: > "$REFLIST"
for sym in $(arm-none-eabi-nm "$IN" 2>/dev/null | awk '{print $NF}' | grep -E '^(m68k|m68ki|YM2612|YM2413)' | sort -u); do
  printf '%s\n' "--redefine-sym=$sym=c1$sym" >> "$REFLIST"
done

if [ -s "$REFLIST" ]; then
  arm-none-eabi-objcopy @"$REFLIST" "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
fi
rm -f "$REFLIST"
exit 0