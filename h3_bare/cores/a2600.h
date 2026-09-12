#ifndef A2600_H
#define A2600_H

#include <stdint.h>
#include "cpu6502.h"

#ifdef __cplusplus
extern "C" {
#endif

// Atari 2600: TIA + RIOT + картридж. Рендер кадра в framebuffer 160x192.
#define A2600_SCANLINES 262
#define A2600_CYCLES_LINE 76

typedef struct {
    cpu6502_t cpu;

    // RIOT
    uint8_t ram[128];
    uint8_t swcha, swchb;

    // TIA
    uint8_t regs[64];
    int  p0_x, p1_x, m0_x, m1_x, bl_x;   // позиции (160px)

    // Видео
    uint16_t* fb;             // framebuffer 160x192 (RGB565)
    int  scanline;
    int  frame_done;
    uint32_t frame;

    const uint8_t* rom;
    uint32_t rom_size;
} a2600_t;

void a2600_init(a2600_t* m, const uint8_t* rom, uint32_t size, uint16_t* fb);
void a2600_reset(a2600_t* m);
void a2600_frame(a2600_t* m);   // 262 строки: CPU + рендер в fb

uint8_t a2600_read(void* ctx, uint16_t addr);
void    a2600_write(void* ctx, uint16_t addr, uint8_t v);

#ifdef __cplusplus
}
#endif

#endif