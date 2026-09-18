#ifndef _STREAMS_FILE_STREAM_H
#define _STREAMS_FILE_STREAM_H
/* Стаб вместо libretro-common: MSX.h включает <streams/file_stream.h>.
 * RFILE = int (индекс + 1 в таблице msx_compat).
 *
 * ВАЖНО: типы retro_log_level / retro_log_printf_t НЕ определяем здесь —
 * они приходят из настоящего libretro.h (включается раньше через
 * NukeYKT/WrapNukeYKT.h в MSX.h). Определение здесь дублировало бы их
 * с другим типом и давало "conflicting types". */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef int RFILE;

extern retro_log_printf_t log_cb;

#define RFILE_HINT_UNBUF 0
#endif