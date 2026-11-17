// r1378: диалог выбора источника для Apple II.
// r1402: добавлен выбор модели — ][+ (48 КБ ОЗУ, ПЗУ 12 КБ) или //e
// (128 КБ: 64 КБ main + 64 КБ aux, ПЗУ 16 КБ, 80 колонок).
//
// Вынесен из main.c в отдельный объект, который линкуется ПОСЛЕДНИМ (см.
// Makefile, конец списка OBJ). Причина: вставка функции в main.c сдвигала
// literal pools и адреса строк во всём .text; на стенде это совпало с отказом
// энумерации USB-клавиатуры (r1376/r1377 — «no keyboard found», r1369 —
// работает). Логика диалога не менялась, добавлен только внешний линк.
#include <stdint.h>
#include "usb_kbd.h"
#include "fb_text.h"

void udelay(uint32_t d);
int  usb_input_poll(void);

// Два шага: 0 — модель машины, 1 — источник (BASIC или образ с SD).
// model (out): 0 = Apple ][+, 1 = Apple //e.
// Возврат: 1 = встроенный Applesoft BASIC (ROM вшит), 2 = образ диска с SD
// (браузер /roms/apple2), 0 = назад (ESC на первом шаге).
int apple2_source_dialog(int *model) {
    static const char *kModel[2] = {
        "1. Apple ][+    48K RAM, 12K ROM",
        "2. Apple //e   128K RAM, 16K ROM, 80 col"
    };
    int stage = 0, sel = 0, dirty = 1, m = 0;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            if (stage == 0) {
                fb_puts_s(60, 90, "Apple II: machine", 2, 0x00FFAA00);
                for (int i = 0; i < 2; i++) {
                    int y = 160 + i * 40;
                    uint32_t clr = (i == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                    if (i == sel) fb_fill_rect(50, y - 6, 760, 30, 0x00222222);
                    fb_puts(70, y, kModel[i], clr);
                }
            } else {
                fb_puts_s(60, 90, m ? "Apple //e: load" : "Apple ][+: load",
                          2, 0x00FFAA00);
                const char *opts[2] = {
                    "1. Applesoft BASIC (built-in)",
                    "2. Disk image (.dsk/.do/.po/.nib/.woz)"
                };
                for (int i = 0; i < 2; i++) {
                    int y = 160 + i * 40;
                    uint32_t clr = (i == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                    if (i == sel) fb_fill_rect(50, y - 6, 760, 30, 0x00222222);
                    fb_puts(70, y, opts[i], clr);
                }
            }
            fb_puts(60, 520, "  ^v: select   Enter: OK   ESC: back", 0x00888888);
            fb_flush(); dirty = 0;
        }
        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (sel > 0) sel--; dirty = 1; }
        else if (k == 81) { if (sel < 1) sel++; dirty = 1; }
        else if (k == 40) {
            if (stage == 0) { m = sel; stage = 1; sel = 0; dirty = 1; }
            else { if (model) *model = m; return (sel == 0) ? 1 : 2; }
        } else if (k == 41 || k == 27) {
            if (stage == 1) { stage = 0; sel = m; dirty = 1; }
            else return 0;
        }
        udelay(50000);
    }
}
