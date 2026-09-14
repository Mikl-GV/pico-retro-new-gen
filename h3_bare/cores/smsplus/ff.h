/* Minimal FatFS types stub for smsplus — no filesystem on pico-retro. */
#ifndef _FF_STUB_H_
#define _FF_STUB_H_

#include <stdint.h>

typedef int FRESULT;
typedef struct { uint8_t dummy; } FIL;

#define FR_OK 0

#endif
