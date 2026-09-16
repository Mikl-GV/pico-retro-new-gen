#ifndef LIBRETRO_MEMORY_STREAM_H
#define LIBRETRO_MEMORY_STREAM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct memstream memstream_t;
memstream_t *memstream_open(uint8_t *data, uint64_t size, unsigned writing);
void memstream_close(memstream_t *stream);
uint64_t memstream_read(memstream_t *stream, void *data, uint64_t bytes);
uint64_t memstream_write(memstream_t *stream, const void *data, uint64_t bytes);
int memstream_getc(memstream_t *stream);
void memstream_putc(memstream_t *stream, int c);
uint64_t memstream_pos(memstream_t *stream);
uint64_t memstream_get_size(memstream_t *stream);
void memstream_rewind(memstream_t *stream);
int64_t memstream_seek(memstream_t *stream, int64_t offset, int whence);
uint64_t memstream_get_ptr(memstream_t *stream);
#ifdef __cplusplus
}
#endif
#endif
