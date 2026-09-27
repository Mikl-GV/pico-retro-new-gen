#!/bin/bash
# Переименовать в gc-объекте blargg-символы, конфликтующие с Lynx.
#
# r0.193: вместо «переименовывать только если lynx_blip_*.o уже собраны» —
# ЖЁСТКИЙ список blargg-токенов. Причина: при параллельной сборке (make -j16)
# часть gc_*.o собиралась ДО lynx_blip_*.o, скрипт выходил без переименования,
# и финальный линк с --allow-multiple-definition склеивал gc-ссылки с
# ЛИНКС-версией Blip_Buffer/Blip_Synth. Итог: кодовые тела разных копий blargg
# в одном образе → рассинхрон внутренних массивов → порча кучи → Data Abort
# (краш Coleco: new Sms_Apu падает в конструкторах членов ДО тела).
#
# Безопасность для других эмуляторов: переименовываются ТОЛЬКО _Z-символы в
# gc-объектах, чьи имена содержат blargg-токены; objcopy --redefine-sym
# согласованно переименовывает и определения, и ссылки внутри gc-объектов.
# Lynx-объекты и прочие ядра (ngp/a7800/snes/...) эти символы не трогают.
IN=$1
if [ ! -f "$IN" ]; then exit 0; fi

TOKENS="Blip_Buffer
Blip_Impulse
Blip_Synth
Effects_Buffer
Multi_Buffer
Stereo_Buffer
Silent_Blip_Buffer"

ARGS=""
# ВАЖНО: брать $NF, а не $3 — у НЕОПРЕДЕЛЁННЫХ (U) символов нет колонки
# адреса, и символ стоит в $2; $NF корректен для обоих видов строк.
for sym in $(arm-none-eabi-nm "$IN" 2>/dev/null | awk '{print $NF}' | grep '^_Z' | sort -u); do
  case "$sym" in
    *_gc) continue ;;
  esac
  for t in $TOKENS; do
    case "$sym" in
      *"$t"*) ARGS="$ARGS --redefine-sym $sym=${sym}_gc"; break ;;
    esac
  done
done

if [ -n "$ARGS" ]; then
  arm-none-eabi-objcopy $ARGS "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
fi
exit 0