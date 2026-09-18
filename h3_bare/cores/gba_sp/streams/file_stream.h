#ifndef FILE_STREAM_H__
#define FILE_STREAM_H__
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef struct RFILE RFILE;
/* Open/read/seek/write/close — в bare-metal всегда неуспех (ROM из памяти).
 * RETRO_VFS_* и SEEK_* уже определены в libretro.h / unistd.h. */
#ifdef __cplusplus
extern "C" {
#endif
RFILE* filestream_open(const char* p, unsigned m, unsigned h);
int64_t filestream_get_size(RFILE* s);
int64_t filestream_read(RFILE* s, void* b, size_t c);
int64_t filestream_write(RFILE* s, const void* b, size_t c);
int filestream_seek(RFILE* s, int64_t o, int w);
int filestream_close(RFILE* s);
#ifdef __cplusplus
}
#endif
#endif