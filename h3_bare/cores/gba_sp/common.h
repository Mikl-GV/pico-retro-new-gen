/* gameplaySP - common.h adapted for H3 bare-metal (Orange Pi Lite).
 * No SDL, no libc stdio/file I/O beyond what libc_min provides. */

#ifndef COMMON_H
#define COMMON_H

#define ror(dest, value, shift) \
  dest = ((value) >> shift) | ((value) << (32 - shift))

#define PATH_SEPARATOR "/"
#define PATH_SEPARATOR_CHAR '/'

#define ARM_ARCH
#define function_cc

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef unsigned char      u8;
typedef signed char        s8;
typedef unsigned short int u16;
typedef signed short int   s16;
typedef unsigned int       u32;
typedef signed int         s32;
typedef unsigned long long int u64;
typedef signed long long int   s64;

#define convert_palette(value) \
  value = ((value & 0x1F) << 11) | ((value & 0x03E0) << 1) | (value >> 10)

/* file I/O — на bare-metal отключено (ROM/BIOS грузит host-слой).
 * Тем не менее код ядра содержит `file_open(tag, ...)` — делаем так, чтобы
 * `tag` объявлялся как int, и все операции были no-op, тогда исходный код
 * ядра компилируется без изменений. */
typedef int FILE;
#define file_open(filename_tag, filename, mode) int filename_tag = 0
#define file_check_valid(filename_tag) (filename_tag)
#define file_close(filename_tag) do { (void)(filename_tag); } while(0)
#define file_read(filename_tag, buffer, size) (0)
#define file_write(filename_tag, buffer, size) (0)
#define file_seek(filename_tag, offset, type) (0)
#define file_tag_type int

/* shim'ы для компиляции */
#define fopen(filename, mode) ((void *)0)
#define fclose(stream) (0)
#define fread(ptr, sz, n, stream) (0)
#define fwrite(ptr, sz, n, stream) (0)
#define fgets(str, n, stream) ((void *)0)
#define fseek(stream, off, whence) (0)
#define ftell(stream) (0)
#define sscanf(...) (0)
#define strcasecmp(a, b) strcmp(a, b)
#define file_length(name, fp) (0)

extern int sprintf(char *buf, const char *fmt, ...);
extern long strtol(const char *nptr, char **endptr, int base);

#define file_read_variable(tag, var)    file_read(tag, &var, sizeof(var))
#define file_write_variable(tag, var)   file_write(tag, &var, sizeof(var))
#define file_read_array(tag, arr)       file_read(tag, arr, sizeof(arr))
#define file_write_array(tag, arr)      file_write(tag, arr, sizeof(arr))
#define file_write_mem(tag, buf, sz)
#define file_write_mem_array(tag, arr)
#define file_write_mem_variable(tag, var)

typedef u32 fixed16_16;
typedef u32 fixed8_24;

#define float_to_fp16_16(value)       (fixed16_16)((value) * 65536.0)
#define fp16_16_to_float(value)       (float)((value) / 65536.0)
#define u32_to_fp16_16(value)         ((value) << 16)
#define fp16_16_to_u32(value)         ((value) >> 16)
#define fp16_16_fractional_part(value) ((value) & 0xFFFF)
#define float_to_fp8_24(value)        (fixed8_24)((value) * 16777216.0)
#define fp8_24_fractional_part(value) ((value) & 0xFFFFFF)
#define fixed_div(n,d,bits) (((n*(1<<bits)) + (d/2)) / d)

#define address8(base, offset)   *((u8  *)((u8 *)base + (offset)))
#define address16(base, offset)  *((u16 *)((u8 *)base + (offset)))
#define address32(base, offset)  *((u32 *)((u8 *)base + (offset)))

/* SDL stubs — everything that gpSP core expects from SDL */
#include "sdl_stubs.h"

#include "cpu.h"
#include "memory.h"
#include "video.h"
#include "input.h"
#include "sound.h"
#include "main.h"
#include "gui.h"
#include "zip.h"
#include "cheats.h"

#include "arm/warm.h"

/* printf via our libc_min -> UART */
extern int printf(const char *fmt, ...);

#endif /* COMMON_H */