#ifndef RK86_I8080_HAL_H
#define RK86_I8080_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// r1185: HAL для i8080.c — реализация в rk86_machine.c.
int  i8080_hal_memory_read_word(int addr);
void i8080_hal_memory_write_word(int addr, int word);
int  i8080_hal_memory_read_byte(int addr);
void i8080_hal_memory_write_byte(int addr, int byte);
int  i8080_hal_io_input(int port);
void i8080_hal_io_output(int port, int value);
void i8080_hal_iff(int on);
unsigned char* i8080_hal_memory(void);
unsigned char* i8080_hal_rom(void);

extern uint32_t i8080_cycles;

#ifdef __cplusplus
}
#endif

#endif
