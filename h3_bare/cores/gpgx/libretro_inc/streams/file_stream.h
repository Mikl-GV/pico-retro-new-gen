#ifndef GPGX_FILE_STREAM_H
#define GPGX_FILE_STREAM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RFILE RFILE;
typedef RFILE file_stream_t;
#define RETRO_VFS_FILE_ACCESS_READ (1 << 0)
#define RETRO_VFS_FILE_ACCESS_HINT_NONE 0
#define RETRO_VFS_SEEK_POSITION_START 0
#define RETRO_VFS_SEEK_POSITION_CURRENT 1
#define RETRO_VFS_SEEK_POSITION_END 2
RFILE *rfopen(const char *path, const char *mode);
int rfclose(RFILE *stream);
int64_t rfread(void *buf, size_t size, size_t count, RFILE *stream);
int64_t rfseek(RFILE *stream, int64_t offset, int whence);
int64_t rftell(RFILE *stream);
int rfgets(char *buf, size_t size, RFILE *stream);
int64_t rfwrite(const void *buf, size_t size, size_t count, RFILE *stream);
int64_t filestream_get_size(RFILE *stream);
static inline RFILE *filestream_open(const char *path, unsigned mode, unsigned hints) {
    (void)mode; (void)hints; return rfopen(path, "rb");
}
static inline void filestream_close(RFILE *s) { rfclose(s); }
static inline int64_t filestream_read(RFILE *s, void *d, uint64_t l) {
    return rfread(d, 1, (size_t)l, s);
}
static inline int64_t filestream_seek(RFILE *s, int64_t o, int w) { return rfseek(s, o, w); }
static inline int64_t filestream_tell(RFILE *s) { return rftell(s); }
#ifdef __cplusplus
}
#endif
#endif
