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
#
# ВАЖНО: применяется к КАЖДОМУ cps1-объекту после компиляции (соответственно
# переименовываются и определения, и ссылки внутри объекта). all m68k/YM2612
# токены перечисляются динамически через nm (как gc_rename.sh по токенам).
IN=$1
[ -f "$IN" ] || exit 0

ARGS=""
for sym in $(arm-none-eabi-nm "$IN" 2>/dev/null | awk '{print $NF}' | grep -E '^(m68k|m68ki|YM2612)' | sort -u); do
  ARGS="$ARGS --redefine-sym $sym=c1$sym"
done

if [ -n "$ARGS" ]; then
  arm-none-eabi-objcopy $ARGS "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
fi
exit 0