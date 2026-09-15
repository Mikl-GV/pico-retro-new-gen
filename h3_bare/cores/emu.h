#ifndef EMU_H
#define EMU_H

#include <stdint.h>

void emu_clear_fb(void);

// Throttle 60 FPS — вызывать раз в кадр
void emu_throttle(void);

// Nearest-neighbour scale: src (в левом верхнем углу EMU_FB) растягивается
// на всю высоту экрана (600), ширина пропорционально, по бокам чёрные поля
void emu_scale(int src_w, int src_h);

// rom_name может быть NULL
void emu_run_a2600(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_portfolio(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_gameboy(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_lynx(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);

#endif