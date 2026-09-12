#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*cpu_read_t)(void* ctx, uint16_t addr);
typedef void    (*cpu_write_t)(void* ctx, uint16_t addr, uint8_t v);

typedef struct {
    uint8_t  a, x, y, sp, sr;
    uint16_t pc;
    int32_t  cycles;
    void*    ctx;
    cpu_read_t  read;
    cpu_write_t write;
} cpu6502_t;

#define CPU_N 0x80
#define CPU_V 0x40
#define CPU_B 0x10
#define CPU_D 0x08
#define CPU_I 0x04
#define CPU_Z 0x02
#define CPU_C 0x01

void cpu6502_reset(cpu6502_t* c);
void cpu6502_run(cpu6502_t* c, int32_t cycles);

#ifdef __cplusplus
}
#endif

#endif