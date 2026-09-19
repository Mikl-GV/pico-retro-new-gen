#ifndef EMU_H3_H
#define EMU_H3_H

#include <stdint.h>

void emu_clear_fb(void);

// Throttle 60 FPS — вызывать раз в кадр
void emu_throttle(void);

// Сброс фазы throttle (вызывать перед циклом кадров системы)
void emu_throttle_reset(void);

// Nearest-neighbour scale: src (в левом верхнем углу EMU_FB) растягивается
// на всю высоту экрана (600), ширина пропорционально, по бокам чёрные поля
void emu_scale(int src_w, int src_h);

// Целочисленный scale (integer scale) для портативных: каждый пиксель
// исходника = N×N на экране, поля по периметру. Максимальный целый множитель,
// влезающий в 1024×600. Чёткая картинка без мыла.
void emu_scale_int(int src_w, int src_h);

// Цвет полей по бокам (XRGB8888). Вызывается перед циклом кадров системы,
// чтобы поля были своего цвета; 0 = чёрный (по умолчанию).
void emu_set_border_color(uint32_t rgb888);

// rom_name может быть NULL
void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_gg(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_portfolio(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_gameboy(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_gba(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_lynx(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_ngp(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_vectrex(const uint8_t* rom, uint32_t size, const char* rom_name);

#endif