#ifndef A2600_H
#define A2600_H

#include "cpu6502.h"

typedef struct {
    emu6502_t cpu;
    uint16_t *fb;
    // ROM mapping
    uint8_t *rom;
    uint32_t rom_size;
    uint32_t bank;   // текущий банк для зоны $F000-$F7FF
    // Определённый маппер (для отладки)
    const char *mapper_label;
} a2600_t;

void a2600_init(a2600_t *m, const uint8_t *rom, uint32_t size, uint16_t *fb);
void a2600_frame(a2600_t *m);
void a2600_set_input(uint8_t joy0_dir, uint8_t joy0_fire);

// Отладочный дамп состояния видеочасти: [0]=vblank [1]=vsync
// [2]=colubk [3]=colupf [4]=grp0 [5]=grp1 [6]=pf0 [7]=pf1
void a2600_get_video(uint8_t out[8]);

#endif