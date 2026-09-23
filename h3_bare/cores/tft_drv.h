// tft_drv.h — TFT DFR0428 (ILI9486) через SPI0 H3.
#ifndef TFT_DRV_H
#define TFT_DRV_H

#include <stdint.h>

#define TFT_W 480
#define TFT_H 320

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация (SPI0 + ILI9486). Возвращает 0 = готово, -1 = дисплей
// не отвечает (загрузку не блокирует).
int  tft_init(void);

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

// Примитивы для рендера меню в tft_fb (RGB565, шрифт 8x8) ----
void tft_render_begin(void);              // очистить буфер, пометить «нужно отправить»
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_puts(int x, int y, const char* s, uint16_t color);
void tft_flush_rect(int x, int y, int w, int h);  // отправить только окно (CASET/RASET)

// Показать на TFT справку по кнопкам: sys_id системы (в меню передавать NULL).
// Пишет один флаг в .coherent; рендер делает TFT-ядро на CPU1.
void tft_help_show(const char* sys_id);

// МОСТ с TFT (нажатия по иконкам справа, touch XPT2046):
// CPU1 пишет сюда код пункта меню при тапе, core0 читает в input_wait меню.
// 0 = нет запроса, -2 = Settings, -4 = About (коды как в menu_run).
extern volatile int32_t g_tft_request;

// ---- TFT core (CPU1) ----
// Флаг «HDMI кадр готов» — ставится в fb_flush (core0), читается TFT-ядром.
// Лежит в .coherent (uncached) — виден между ядрами сразу.
extern volatile uint32_t g_tft_frame_ready;
// Зацикленный процессор TFT: init дисплея + зеркалирование HDMI-кадра.
// Запускается на CPU1 через cpu1_entry (startup.S) + h3_cpu_start.
void tft_core_main(void);

#ifdef __cplusplus
}
#endif

#endif