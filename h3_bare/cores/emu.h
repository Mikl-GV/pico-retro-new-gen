#ifndef EMU_H3_H
#define EMU_H3_H

#include <stdint.h>

void emu_clear_fb(void);
void snd_manifest(const char* sys, const char* cores);   // r0.385: звуковой манифест (лог)

// Единая подготовка ПЕРЕД запуском любого эмулятора: сброс bump-пула ядровых
// malloc + очистка EMU_FB и HDMI-кадра. Вызывать в начале каждого emu_run_*,
// иначе при повторном входе на экране остаётся «загруженное до этого».
void emu_prepare(void);
void emu_audio_stop(void);   // r939: финализация звука на выходе (кольцо+sounder)

// Throttle 60 FPS — вызывать раз в кадр
void emu_throttle(void);

// Сброс фазы throttle (вызывать перед циклом кадров системы)
void emu_throttle_reset(void);

// Задать период кадра throttle в мкс (по умолчанию 16667 = 60 Гц).
// Системы с PAL-частотой (C64/C128 50 Гц) ставят свой период до цикла кадров,
// чтобы throttle не «догонял» чужую частоту и не копил переработку (r989).
void emu_set_period_us(uint32_t us);

// Единый выход по удержанию ESC (~0.9 с). Вызывать раз в кадр в цикле
// эмулятора; 1 = пора выходить. emu_esc_hold_reset() — перед входом в цикл.
void emu_esc_hold_reset(void);
int  emu_esc_hold(void);

// Nearest-neighbour scale: src (в левом верхнем углу EMU_FB) растягивается
// на всю высоту экрана (600), ширина пропорционально, по бокам чёрные поля
void emu_scale(int src_w, int src_h);

// Целочисленный scale (integer scale) для портативных: каждый пиксель
// исходника = N×N на экране, поля по периметру. Максимальный целый множитель,
// влезающий в 1024×600. Чёткая картинка без мыла.
void emu_scale_int(int src_w, int src_h);

// r1170: сбросить кэш геометрии целочисленного скейла (бордюры будут залиты
// заново на следующем кадре). Нужен после полной очистки экрана.
void emu_scale_int_invalidate(void);

// Цвет полей по бокам (XRGB8888). Вызывается перед циклом кадров системы,
// чтобы поля были своего цвета; 0 = чёрный (по умолчанию).
void emu_set_border_color(uint32_t rgb888);

// r703 (C3): double-buffer HDMI для систем, пишущих напрямую в HDMI FB
// (BK-0010 / Vectrex). ВЫКЛЮЧЕН по умолчанию (CONFIG_HDMI_DOUBLE_BUF=0) — DE2/TCON1
// в этом биндинге не дают дешёвого vsync-флага, безусловный flip рискует сломать
// уже работающий HDMI на стенде. При 0 — no-op (поведение прошивки не меняется).
// При 1 — bk/vecx пишут в back buffer, а emu_hdmi_flip() переключает BOT_LADDR.
void emu_hdmi_flip(void);

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
void emu_run_32x(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_vectrex(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_pce(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_fuse(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_bk(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_cps1(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_ms1504(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_cps2(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_neogeo(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_toaplan(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_cave(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_segasys(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_fbneo(const uint8_t* rom, uint32_t size, const char* rom_name);
void emu_run_atari800(const uint8_t* rom, uint32_t size, const char* rom_name);   // r778
void emu_run_cpc(const uint8_t* rom, uint32_t size, const char* rom_name);        // r963
void emu_run_c64(const uint8_t* rom, uint32_t size, const char* rom_name);        // r974

#endif