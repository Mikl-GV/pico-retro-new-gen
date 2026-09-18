/* msx_log.c — определяет log_cb (используется ядром fMSX).
 * Тип retro_log_printf_t берём из настоящего libretro.h. */
#include <libretro.h>
#include <streams/file_stream.h>

retro_log_printf_t log_cb = 0;