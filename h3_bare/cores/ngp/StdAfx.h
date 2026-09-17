#ifndef STDAFX_H
#define STDAFX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int DWORD;
typedef unsigned char byte;
typedef unsigned char _u8;
typedef unsigned short _u16;
typedef unsigned int _u32;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define HWND void*
#define _MAX_PATH 128
#define MB_OK 1

#define dbg_printf(...)
#define dbg_print(...)

#endif