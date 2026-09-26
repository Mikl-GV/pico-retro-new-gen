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

// МОСТ с TFT (нажатия по иконкам справа, touch TSC2046I):
// r124: код ушёл в SRAM-почту (TFT_BTN=0x64) — см. tft_drv.c.
// 0 = нет запроса, -2 = Settings, -4 = About (коды как в menu_run).

// ---- Тач-отладка (P3/P4): CPU1 пишет, core0 выводит в UART ----
// Драки за UART нет: CPU1 printf не зовёт, только кладёт данные сюда.
extern volatile uint16_t g_ts_rx;        // сырой X TSC2046I (0x90)
extern volatile uint16_t g_ts_ry;        // сырой Y TSC2046I (0xD0)
extern volatile int16_t  g_ts_px;        // масштабированный x (0..TFT_W-1)
extern volatile int16_t  g_ts_py;        // масштабированный y (0..TFT_H-1)
extern volatile uint32_t g_ts_seq;       // растёт на КАЖДОМ событии (P4: повтор при удержании)
extern volatile uint8_t  g_ts_pressed;   // 1 = сейчас нажат

// r53: диагностика зажатого CS
extern volatile uint32_t g_ts_dbg_flag;
extern volatile uint32_t g_ts_dbg_data[8];

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