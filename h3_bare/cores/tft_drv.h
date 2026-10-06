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

// r726: глобалы тача g_ts_rx/ry/px/py/seq/pressed/dbg_* УДАЛЕНЫ — их никто не
// читал (CPU1 передаёт попадание в иконку через SRAM-почту TFT_BTN, калибровку —
// через g_cal_*/g_touch_*).

// ---- TFT core (CPU1) ----
// Зацикленный процессор TFT: init дисплея + справка + тач.
// Запускается на CPU1 через cpu1_entry (startup.S) + h3_cpu_start.
void tft_core_main(void);

#ifdef __cplusplus
}
#endif

#endif