#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum retro_log_level {
    RETRO_LOG_WARN = 2,
    RETRO_LOG_ERROR = 3,
    RETRO_LOG_INFO = 1,
};

#define RETRO_VFS_FILE_ACCESS_READ (1 << 0)
#define RETRO_VFS_FILE_ACCESS_HINT_NONE 0
#define RETRO_VFS_SEEK_POSITION_START 0
#define RETRO_VFS_SEEK_POSITION_CURRENT 1
#define RETRO_VFS_SEEK_POSITION_END 2

typedef struct RFILE RFILE;

RFILE* filestream_open(const char* path, unsigned mode, unsigned hints);
void filestream_close(RFILE* stream);
int64_t filestream_read(RFILE* stream, void* data, uint64_t len);
int64_t filestream_seek(RFILE* stream, int64_t offset, int whence);
int64_t filestream_tell(RFILE* stream);
int64_t filestream_get_size(RFILE* stream);

int path_is_valid(const char* path);
int string_is_empty(const char* s);

typedef struct memstream memstream_t;
memstream_t* memstream_open(uint8_t* data, uint64_t size, unsigned writing);
void memstream_close(memstream_t* stream);
uint64_t memstream_read(memstream_t* stream, void* data, uint64_t bytes);
uint64_t memstream_write(memstream_t* stream, const void* data, uint64_t bytes);
uint64_t memstream_pos(memstream_t* stream);
int64_t memstream_seek(memstream_t* stream, int64_t offset, int whence);

#ifdef __cplusplus
}
#endif
#endif