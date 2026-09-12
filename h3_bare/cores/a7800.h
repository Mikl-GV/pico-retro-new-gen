#ifndef A7800_H
#define A7800_H

#include <stdint.h>
#include "cpu6502.h"

#ifdef __cplusplus
extern "C" {
#endif

// Atari 7800: MARIA (DLL -> DL) + TIA + RIOT. Рендер кадра из DLL в fb 320x240.
#define A7800_TV_W 320
#define A7800_TV_H 240
#define A7800_FRAME_CYCLES 29800

typedef struct {
    cpu6502_t cpu;
    uint8_t  ram_low[256];
    uint8_t  ram_main[0x1000];     // $1800-$27FF
    uint8_t  riot_ram[128];
    uint8_t  tia[64];
    uint8_t  maria[64];            // $20-$3F

    const uint8_t* rom;            // картридж (заголовок 128 b снят)
    uint32_t rom_size;
    const uint8_t* bios;
    uint16_t bios_base, bios_size;

    uint16_t dll_ptr;
    uint16_t* fb;                  // 320x240 RGB565
    int  frame_done;
    uint32_t frame;
} a7800_t;

void a7800_init(a7800_t* m, const uint8_t* rom, uint32_t size,
                const uint8_t* bios, uint16_t bios_size, uint16_t* fb);
void a7800_reset(a7800_t* m);
void a7800_frame(a7800_t* m);

uint8_t a7800_read(void* ctx, uint16_t addr);
void    a7800_write(void* ctx, uint16_t addr, uint8_t v);

#ifdef __cplusplus
}
#endif

#endif