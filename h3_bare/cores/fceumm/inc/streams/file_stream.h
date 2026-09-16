#ifndef LIBRETRO_FILE_STREAM_H
#define LIBRETRO_FILE_STREAM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RFILE RFILE;
#define RETRO_VFS_FILE_ACCESS_READ (1 << 0)
#define RETRO_VFS_FILE_ACCESS_HINT_NONE 0
#define RETRO_VFS_SEEK_POSITION_START 0
#define RETRO_VFS_SEEK_POSITION_CURRENT 1
#define RETRO_VFS_SEEK_POSITION_END 2
RFILE *filestream_open(const char *path, unsigned mode, unsigned hints);
void filestream_close(RFILE *stream);
int64_t filestream_read(RFILE *stream, void *data, uint64_t len);
int64_t filestream_seek(RFILE *stream, int64_t offset, int whence);
int64_t filestream_tell(RFILE *stream);
int64_t filestream_get_size(RFILE *stream);
#ifdef __cplusplus
}
#endif
#endif
