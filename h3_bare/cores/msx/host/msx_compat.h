// msx_compat.h — публичные API стабов для порта fMSX
#ifndef MSX_COMPAT_H
#define MSX_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Подать буфер картриджа хосту (CARTA.ROM)
void msx_compat_set_cart(const uint8_t* rom, uint32_t size);

// RFILE = opaque int (дескриптор открытого файла в памяти)
typedef int RFILE;

#define RFILE_HINT_UNBUF 0

// rf* — стаб поверх встроенных BIOS и FAT SD
RFILE* rfopen(const char* path, const char* mode);
int    rfclose(RFILE* stream);
int64_t rfread(void* data, size_t elem_size, size_t elem_count, RFILE* stream);
int64_t rfwrite(const void* data, size_t elem_size, size_t elem_count, RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int whence);
int64_t rftell(RFILE* stream);
int     rfeof(RFILE* stream);
int     rfgetc(RFILE* stream);
int     rfputc(int c, RFILE* stream);
char*   rfgets(char* s, int size, RFILE* stream);
void    filestream_rewind(RFILE* stream);

// libc-дополнения
int     sscanf(const char* str, const char* fmt, ...);
char*   strcasestr(const char* h, const char* n);
int     chdir(const char* path);
char*   getcwd(char* buf, size_t size);
time_t  time(time_t* t);
struct tm* localtime(const time_t* t);
size_t  strlcpy(char* dst, const char* src, size_t siz);
void    fill_pathname_join(char* dst, const char* a, const char* b, size_t size);

#ifdef __cplusplus
}
#endif

#endif