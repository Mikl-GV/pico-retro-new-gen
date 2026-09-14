#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

typedef struct {
    uint16_t pc;
    uint8_t  a, x, y, sp, p;
    uint32_t cycles;
} cpu6502_t;

// Флаги P
#define FLAG_N 0x80
#define FLAG_V 0x40
#define FLAG_U 0x20
#define FLAG_B 0x10
#define FLAG_D 0x08
#define FLAG_I 0x04
#define FLAG_Z 0x02
#define FLAG_C 0x01

typedef struct {
    cpu6502_t cpu;
    uint8_t (*read)(void* ctx, uint16_t addr);
    void  (*write)(void* ctx, uint16_t addr, uint8_t val);
    void* mem_ctx;
} emu6502_t;

void cpu6502_init(emu6502_t* e);
void cpu6502_exec(emu6502_t* e, uint32_t max_cycles);

#endif