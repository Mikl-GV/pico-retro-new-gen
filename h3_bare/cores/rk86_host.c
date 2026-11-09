// r1185: Радио-86РК — host-слой H3 (порт эмулятора RK8266 на bare-metal).
//
// Ядро CPU — i8080.c (Alexander Demin, GPL-2+), машина — rk86_machine.c
// (карта памяти/портов, ВВ55, ВГ75, ВТ57), рендер — rk86_video.c.
// Видео выводится напрямую в HDMI-фреймбуфер (1024x600 XRGB8888), как у БК:
// экран 78x30 знакомест 6x8 с межстрочным интервалом 2 пикселя растягивается
// ровно вдвое (936x600, поля по 44 пикселя). Звука у машины нет.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "emu.h"
#include "fb_text.h"
#include "usb_kbd.h"
#include "btn_pad.h"

#include "rk86/rk86_machine.h"
#include "rk86/rk86_video.h"

#define RK86_FRAME_US 20000u          // 50 Гц
#define RK86_CYCLES_PER_FRAME 35600u  // 1.78 МГц / 50 Гц
#define RK86_WARM_FRAMES 10           // прогрев монитора до загрузки программы

// Раскладка: физическая клавиша PC -> позиция матрицы РК.
// Таблица повторяет kb_rus из RK8266 (проверена на железе): в РУС-режиме
// клавиши ЙЦУКЕН выдают буквы РК, в ЛАТ — латинские по тем же позициям.
static const uint16_t rk_rus[26] = {
    // a     b     c     d     e     f     g     h
    RK86_F, RK86_I, RK86_S, RK86_W, RK86_U, RK86_A, RK86_P, RK86_R,
    // i     j     k     l     m     n     o     p
    RK86_O, RK86_J, RK86_L, RK86_D, RK86_X, RK86_T, RK86_N, RK86_G,
    // q     r     s     t     u     v     w     x
    RK86_Q, RK86_K, RK86_Y, RK86_E, RK86_M, RK86_C, RK86_CARET, RK86_Z,
    // y     z
    RK86_Z, RK86_LBRK,
};

static const uint16_t rk_lat[26] = {
    RK86_A, RK86_B, RK86_C, RK86_D, RK86_E, RK86_F,
    RK86_G, RK86_H, RK86_I, RK86_J, RK86_K, RK86_L,
    RK86_M, RK86_N, RK86_O, RK86_P, RK86_Q, RK86_R,
    RK86_S, RK86_T, RK86_U, RK86_V, RK86_W, RK86_X,
    RK86_Y, RK86_Z,
};

static uint16_t hid_to_rk(uint8_t u, int rus)
{
    if (u >= 0x04 && u <= 0x1D)
        return rus ? rk_rus[u - 0x04] : rk_lat[u - 0x04];

    // Цифры: HID 0x1E..0x27 -> RK_1..RK_0 (матрица 0,1..7 и 8,9).
    if (u >= 0x1E && u <= 0x27) {
        static const uint16_t d[10] = {
            RK86_1, RK86_2, RK86_3, RK86_4, RK86_5,
            RK86_6, RK86_7, RK86_8, RK86_9, RK86_0,
        };
        return d[u - 0x1E];
    }

    switch (u) {
    case 0x28: return RK86_VK;        // Enter
    case 0x58: return RK86_PS;        // KP Enter
    case 0x2A: return RK86_ZB;        // Backspace
    case 0x2B: return RK86_TAB;       // Tab
    case 0x2C: return RK86_SPACE;     // Space
    case 0x2D: return RK86_MINUS;     // -
    case 0x2E: return RK86_MINUS;     // = (без Shift)
    case 0x2F: return rus ? RK86_H : RK86_LBRK;   // [
    case 0x30: return rus ? RK86_X : RK86_RBRK;   // ]
    case 0x31: return rus ? RK86_BSLASH : RK86_BSLASH;
    case 0x33: return rus ? RK86_V : RK86_SEMI;   // ;
    case 0x34: return rus ? RK86_BSLASH : RK86_CARET;
    case 0x36: return rus ? RK86_B : RK86_COMMA;  // ,
    case 0x37: return rus ? RK86_AT : RK86_PERIOD; // .
    case 0x4F: return RK86_RIGHT;
    case 0x50: return RK86_LEFT;
    case 0x51: return RK86_DOWN;
    case 0x52: return RK86_UP;
    case 0x3A: return RK86_F1;
    case 0x3B: return RK86_F2;
    case 0x3C: return RK86_F3;
    case 0x3D: return RK86_F4;
    case 0x4A: return RK86_HOME;
    default:   return 0;
    }
}

static int usage_in(const uint8_t* keys, int n, uint8_t u)
{
    for (int i = 0; i < n; i++)
        if (keys[i] == u) return 1;
    return 0;
}

static void rk86_input_update(uint8_t* prev, int* prev_n, int* rus, int* caps_prev)
{
    uint8_t keys[8] = {0};
    int n = usb_kbd_get_raw(keys, 8);
    if (n > 8) n = 8;

    // Отпускания: были в прошлом отчёте, нет в текущем.
    for (int i = 0; i < *prev_n; i++) {
        if (!prev[i]) continue;
        if (!usage_in(keys, n, prev[i])) {
            uint16_t c = hid_to_rk(prev[i], *rus);
            if (c) rk86_kbd_release(c);
        }
    }

    // Нажатия: есть сейчас, не было раньше.
    for (int j = 0; j < n; j++) {
        if (!keys[j]) continue;
        if (!usage_in(prev, *prev_n, keys[j])) {
            uint16_t c = hid_to_rk(keys[j], *rus);
            if (c) rk86_kbd_press(c);
        }
    }

    memcpy(prev, keys, n);
    *prev_n = n;

    // CapsLock — переключение РУС/ЛАТ (в РК это отдельная клавиша).
    int caps = usage_in(keys, n, 0x39);
    if (caps && !*caps_prev) {
        *rus = !*rus;
        rk86_kbd_release_all();
    }
    *caps_prev = caps;

    uint8_t mods = usb_kbd_get_mods();
    rk86_kbd_set_mods((mods & 0x02) || (mods & 0x20),   // Shift -> СС
                      (mods & 0x01) || (mods & 0x10),   // Ctrl  -> УС
                      0);                               // РУС/ЛАТ — CapsLock
}

void emu_run_rk86(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    emu_prepare();
    fb_flip_set(0);
    snd_manifest("rk86", "none");

    // Образ ПЗУ (≤2К) заменяет монитор; всё, что больше, — программа:
    // монитор инициализирует ВГ75/переменные, затем грузим образ и прыгаем
    // на его стартовый адрес (форматы .rk/.rkr/.gam/.pki/.bin).
    if (rom && size > 0x800) {
        rk86_init(NULL, 0);
        for (int i = 0; i < RK86_WARM_FRAMES; i++) {
            rk86_run(RK86_CYCLES_PER_FRAME);
            rk86_vg75_vsync();
        }
        uint32_t e = rk86_load_program(rom, size, rom_name);
        printf("RK86: \"%s\" size=%d entry=%04X%s\n",
               rom_name ? rom_name : "?", (int)size,
               e ? (unsigned)(e - 1) : 0, e ? "" : " (not a program)");
    } else {
        rk86_init(rom, size);
        printf("RK86: \"%s\" size=%d (ROM/monitor)\n",
               rom_name ? rom_name : "monitor", (int)size);
    }

    emu_set_border_color(0x00000000);
    emu_set_period_us(RK86_FRAME_US);
    emu_throttle_reset();
    emu_esc_hold_reset();

    uint8_t prev[8] = {0};
    int prev_n = 0;
    int rus = 1;
    int caps_prev = 0;
    uint32_t blink = 0;

    for (;;) {
        rk86_input_update(prev, &prev_n, &rus, &caps_prev);

        rk86_run(RK86_CYCLES_PER_FRAME);
        rk86_vg75_vsync();

        rk86_render((uint32_t*)fb_draw_addr(), (int)blink);

        blink++;
        emu_throttle();
        fb_flush();

        if (emu_esc_hold()) break;
    }

    emu_audio_stop();
    fb_clear();
    fb_flush();
}
