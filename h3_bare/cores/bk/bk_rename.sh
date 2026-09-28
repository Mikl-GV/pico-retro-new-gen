#!/bin/bash
# bk_rename.sh — согласованно (defs+refs во всех bk-объектах) переименовать
# символы ядра BK, конфликтующие с PCE (plain retro_*) и другими ядрами.
#
# r0.252: к retro_* добавлены ГЛОБАЛЬНЫЕ callback-переменные ядра —
#   log_cb, input_state_cb, environ_cb, video_cb (и др.). Они объявлены в
#   libretro.c, но читаются/пишутся и из tty-libretro.c через extern.
#   У PCE/Fuse есть СВОИ глобальные с теми же именами; --allow-multiple-definition
#   отдавал имя первому, и ссылки tty-libretro.o привязывались к ЧУЖОЙ ячейке,
#   которую bk_retro_set_input_state не заполняет → NULL → blx 0 (prefetch abort).
IN=$1
[ -f "$IN" ] || exit 0
SYMS="log_cb input_state_cb environ_cb video_cb audio_cb audio_batch_cb \
 input_poll_cb io_init sound_init tape_write joystick_init mouse_read mouse_write disk_write \
 retro_api_version retro_cheat_reset retro_cheat_set retro_deinit \
 retro_get_memory_data retro_get_memory_size retro_get_region retro_get_system_av_info \
 retro_get_system_info retro_init retro_load_game retro_load_game_special retro_reset \
 retro_run retro_serialize retro_serialize_size retro_set_audio_sample \
 retro_set_audio_sample_batch retro_set_controller_port_device retro_set_environment \
 retro_set_input_poll retro_set_input_state retro_set_video_refresh retro_unload_game \
 retro_unserialize"
ARGS=""
for s in $SYMS; do ARGS="$ARGS --redefine-sym $s=bk_$s"; done
arm-none-eabi-objcopy $ARGS "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
exit 0
