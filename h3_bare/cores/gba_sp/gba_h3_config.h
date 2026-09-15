// gba_h3_config.h — конфигурация gpSP для H3 bare-metal
#ifndef GBA_H3_CONFIG_H
#define GBA_H3_CONFIG_H

#define ARM_ARCH
#define ARM_ARCH_7
#define RPI_BUILD

#define function_cc

#define PATH_SEPARATOR "/"
#define PATH_SEPARATOR_CHAR '/'

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short int u16;
typedef signed short int s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long int u64;
typedef signed long long int s64;

#define convert_palette(value) \
    value = ((value & 0x1F) << 11) | ((value & 0x03E0) << 1) | (value >> 10)

#define file_open(tag, fn, mode)
#define file_check_valid(tag) (0)
#define file_close(tag)
#define file_read(tag, buf, sz) (0)
#define file_write(tag, buf, sz) (0)
#define file_seek(tag, off, type) (0)
#define file_tag_type int

#define file_read_variable(tag, var) file_read(tag, &var, sizeof(var))
#define file_write_variable(tag, var) file_write(tag, &var, sizeof(var))
#define file_read_array(tag, arr) file_read(tag, arr, sizeof(arr))
#define file_write_array(tag, arr) file_write(tag, arr, sizeof(arr))

#define file_write_mem(tag, buf, sz)
#define file_write_mem_array(tag, arr)
#define file_write_mem_variable(tag, var)

#endif