// Минимальная заглушка libretro-common streams/file_stream.h для Stella 2014.
// KidVid.hxx включает этот хедер и использует RFILE/rfopen/rfclose/rfseek/
// rfgetc — редкий аксессуар (плеер сэмплов), в bare-metal недоступен.
// Определяем типы и прототипы как в libretro-common, сами функции — no-op
// в a2600_compat.c (ничего не открывают: KidVid в сборке H3 не активен).
#ifndef LIBRETRO_FILE_STREAM_H
#define LIBRETRO_FILE_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RFILE RFILE;

RFILE* rfopen(const char* path, const char* mode);
int    rfclose(RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int origin);
int    rfgetc(RFILE* stream);
int64_t rftell(RFILE* stream);
size_t rfread(void* buffer, size_t elem_size, size_t elem_count, RFILE* stream);
int    rfwrite(const void* buffer, size_t elem_size, size_t elem_count, RFILE* stream);
int    rfeof(RFILE* stream);
int    rferror(RFILE* stream);

#ifdef __cplusplus
}
#endif

#endif