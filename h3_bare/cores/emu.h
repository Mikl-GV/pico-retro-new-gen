#ifndef EMU_H
#define EMU_H

#include <stdint.h>

void emu_clear_fb(void);

// Throttle 60 FPS — вызывать раз в кадр
void emu_throttle(void);

// rom_name может быть NULL
void emu_run_a2600(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);

#endif