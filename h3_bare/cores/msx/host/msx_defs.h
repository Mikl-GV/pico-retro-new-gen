// msx_defs.h — конфигурация буфера кадра и палитры для порта fMSX
// Включается ТОЛЬКО из msx_host.c, до CommonMux.h.
#ifndef MSX_DEFS_H
#define MSX_DEFS_H

#include <stdint.h>
#include <stdbool.h>

// Размеры кадра (должны совпадать с libretro.c)
#define SND_RATE 48000
#define BORDER 8
#define WIDTH  (256 + (BORDER << 1))   // 272
#define HEIGHT (212 + (BORDER << 1))   // 228
#define MAX_HEIGHT   (256 + BORDER)    // 264
#define MAX_SCANLINE (255)             // NTSC максимум (PALVideo? 255)

// RGB565 (порядок как в libretro.c для ARM: R5 G6 B5)
#define PIXEL(R,G,B) (uint16_t)((((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255)))

// ---- символы, которые должен предоставить host (Common.h/Wide.h ссылаются) ----
uint16_t XPal[80];
uint16_t BPal[256];
uint16_t XPal0;
int      LastScanline;

// Линия кадра: 256/512 (Wide) или 512 (interlace)
static int  OverscanMode = 0;
static unsigned frame_number = 0;
static int hires_mode = 0;
#define HiResMode        (0)
#define InterlacedMode   (0)
#define OverscanMode     (OverscanMode)
#define OddPage          (frame_number & 1)

// Буфер кадра: 512x264 (max) пикселей RGB565
static uint16_t image_buffer[WIDTH > 512 ? WIDTH : 512][MAX_HEIGHT];
static unsigned image_buffer_width  = WIDTH;
static unsigned image_buffer_height = HEIGHT;

#define XBuf image_buffer
#define WBuf image_buffer

// RefreshLine[] — таблица в MSX.c (extern), рендер-функции из CommonMux.h
extern void (*RefreshLine[14])(uint8_t Y);

#endif