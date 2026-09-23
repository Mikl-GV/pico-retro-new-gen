// tft_drv.h — TFT DFR0428 (ILI9486) через SPI0 H3.
#ifndef TFT_DRV_H
#define TFT_DRV_H

#include <stdint.h>

#define TFT_W 480
#define TFT_H 320

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация (SPI0 + ILI9486)
void tft_init(void);

// Однократная полная отправка текущего источника (блокирующая, для теста)
void tft_flush(void);

// Порционный вызов из главного цикла (не блокирует)
void tft_tick(void);

// Источник кадра для TFT:
//  tft_set_menu_mode() — рисовать из внутреннего буфера tft_fb (рендер меню)
//  tft_set_dup_mode()  — даунскейл HDMI-фреймбуфера (эмуляторы/игры)
void tft_set_menu_mode(void);
void tft_set_dup_mode(void);

// Диагностика: залить весь экран сплошным цветом (проверка SPI/RAMWR)
void tft_test_red(void);
void tft_test_fill(uint16_t color);

// ---- Примитивы для рендера меню в tft_fb (RGB565, шрифт 8x8) ----
void tft_render_begin(void);              // очистить буфер, пометить «нужно отправить»
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_puts(int x, int y, const char* s, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif