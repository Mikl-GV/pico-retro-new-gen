#ifndef _FILE_STREAM_TRANSFORMS_H
#define _FILE_STREAM_TRANSFORMS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
/* RFILE = int (наша реализация — таблица открытых буферов в памяти) */
typedef int RFILE;
#define RFILE_HINT_UNBUF 0
#define FILESTREAM_OPEN_REQUIRED 1
#define FILESTREAM_OPEN_HINT_NONE 0
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
#endif
