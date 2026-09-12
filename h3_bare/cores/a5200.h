#ifndef A5200_H
#define A5200_H

#include <stdint.h>
#include "cpu6502.h"

#ifdef __cplusplus
extern "C" {
#endif

// Atari 5200: ANTIC + GTIA + POKEY. Рендер кадра из display list.
// Рендер 320x192 в framebuffer (RGB565). Без BIOS не стартует — векторы в BIOS.
#define A5200_TV_W 320
#define A5200_TV_H 192
#define A5200_FRAME_CYCLES 29800

typedef struct {
    cpu6502_t cpu;
    uint8_t  ram[0x4000];          // 16K
    const uint8_t* rom;            // картридж $4000-$7FFF
    uint32_t rom_size;
    const uint8_t* bios;           // BIOS 2K @ $F800
    uint8_t  gtia[64];
    uint8_t  antic[64];
    uint8_t  pokey[64];

    uint16_t dlist;
    uint16_t chbase;
    uint16_t pmbase;
    uint8_t  dmactl;

    uint16_t* fb;                  // 320x192 RGB565
    int  frame_done;
    uint32_t frame;
} a5200_t;

void a5200_init(a5200_t* m, const uint8_t* rom, uint32_t size,
                const uint8_t* bios, uint16_t* fb);
void a5200_reset(a5200_t* m);
void a5200_frame(a5200_t* m);

uint8_t a5200_read(void* ctx, uint16_t addr);
void    a5200_write(void* ctx, uint16_t addr, uint8_t v);

#ifdef __cplusplus
}
#endif

#endif