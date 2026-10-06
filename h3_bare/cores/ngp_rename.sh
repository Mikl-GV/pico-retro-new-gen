#!/bin/bash
# ngp_rename.sh — переименовать конфликтующие символы в ngp-объектах
# для единой линковки с --allow-multiple-definition.
#
# r725: sound_init/sound_update в RACE (NGP) имеют ДРУГУЮ сигнатуру, чем
# в Genesis Plus GX (gpgx/core/sound/sound.c):
#   NGP  (neopopsound.c):  void sound_init(int),  void sound_update(u16*, int)
#   GPGX (sound.c):        void sound_init(void), int  sound_update(unsigned)
# --allow-multiple-definition выбирал ОДНО тело на оба имени (GPGX-версию),
# а ngp_host вызывает sound_init(44100)/sound_update(buf,bytes) — вызов с
# аргументами против функции без параметров = UB (порча регистров/стека).
# Переименовываем во ВСЕХ ngp-объектах в ngp_sound_init/ngp_sound_update
# (согласованно: определения neopopsound и ссылки ngp_host внутри набора).
#
# Принимает ЛЮБОЕ число файлов (make передаёт список NGP_OBJS); обрабатывает
# каждый, у кого есть символы sound_init/sound_update.
for IN in "$@"; do
  [ -f "$IN" ] || continue

  if arm-none-eabi-nm "$IN" 2>/dev/null | grep -qE ' (T|U) (sound_init|sound_update)$'; then
    arm-none-eabi-objcopy \
      --redefine-sym sound_init=ngp_sound_init \
      --redefine-sym sound_update=ngp_sound_update \
      "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
  fi
done
exit 0