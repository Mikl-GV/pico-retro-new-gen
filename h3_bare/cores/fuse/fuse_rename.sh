#!/bin/bash
# fuse_rename.sh — согласованно (defs+refs во всех fuse-объектах) переименовать
# символы Fuse, конфликтующие с PCE и другими ядрами: libretro-API (retro_*),
# vendored libretro-common (filestream_*/crc32/…), а также z80_init/sound_init/RAM/palette.
IN=$1
[ -f "$IN" ] || exit 0
SYMS="crc32 filestream_close filestream_gets filestream_get_size filestream_open filestream_read filestream_read_file filestream_seek filestream_tell filestream_vfs_init fill_pathname_join log_cb memory_map_read palette path_is_valid RAM retro_api_version retro_cheat_reset retro_cheat_set retro_deinit retro_get_memory_data retro_get_memory_size retro_get_region retro_get_system_av_info retro_get_system_info retro_init retro_load_game retro_load_game_special retro_reset retro_run retro_serialize retro_serialize_size retro_set_audio_sample retro_set_audio_sample_batch retro_set_controller_port_device retro_set_environment retro_set_input_poll retro_set_input_state retro_set_video_refresh retro_unload_game retro_unserialize sound_init string_trim_whitespace string_trim_whitespace_right strlcpy_retro__ z80_init z80_reset "
ARGS=""
for s in $SYMS; do ARGS="$ARGS --redefine-sym $s=fuse_$s"; done
arm-none-eabi-objcopy $ARGS "$IN" "$IN.tmp" && mv "$IN.tmp" "$IN"
exit 0
