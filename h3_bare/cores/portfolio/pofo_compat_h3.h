// pofo_compat_h3.h — compat-слой Portfolio поверх H3.
// Эмулирует display.h + joypad.h + hardware/uart.h + hardware/timer.h
// оригинального V4 через H3 API, чтобы system_portfolio.cpp не вязался с Pico SDK.
#ifndef POFO_COMPAT_H3_H
#define POFO_COMPAT_H3_H

#include <stdint.h>

// C-функции платформы обязаны иметь C-линковку в C++-контексте
extern "C" {
#include "uart.h"
#include "h3_hs_timer.h"
#include "usb_kbd.h"
#include "fb_text.h"
}

#define RGB565(r, g, b) ((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F))

// Рендер Portfolio идёт В ОБЩИЙ emu-буфер 320×240 RGB565 (EMU_FB).
// emu_run_portfolio() масштабирует его на весь экран через emu_scale(320,240).
#define POFO_FB   ((uint16_t*)0x5F800000)
#define POFO_W    320
#define POFO_H    240

// 1. display_fill: залить весь буфер
static inline void display_fill(uint16_t color) {
    for (int y = 0; y < POFO_H; y++)
        for (int x = 0; x < POFO_W; x++)
            POFO_FB[y * POFO_W + x] = color;
}

// 2. display_fill_rect
static inline void display_fill_rect(int x0, int y0, int w, int h, uint16_t color) {
    for (int y = y0; y < y0 + h && y < POFO_H; y++) {
        if (y < 0) continue;
        for (int x = x0; x < x0 + w && x < POFO_W; x++) {
            if (x < 0) continue;
            POFO_FB[y * POFO_W + x] = color;
        }
    }
}

// 3. display_stream_begin / pixels16 / end — LCD-строка в общий EMU_FB
static inline void display_stream_begin(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
static int g_stream_y = 0;

// Переопределяем макросы для Portfolio: stream_begin запоминает y, pixels16 пишет
#undef display_stream_begin
#undef display_stream_pixels16
#undef display_stream_end
#define display_stream_begin(x, y, w, h) do { g_stream_y = (y); } while(0)
#define display_stream_pixels16(src, w, h) do { \
    int sy_ = g_stream_y; \
    for (int dx_ = 0; dx_ < (w); dx_++) { \
        if (sy_ >= 0 && sy_ < POFO_H && dx_ < POFO_W) \
            POFO_FB[sy_ * POFO_W + dx_] = (src)[dx_]; \
    } \
} while(0)
#define display_stream_end() do {} while(0)

// 4. display_stream_pixels — не используется Portfolio, но объявим пустым
static inline void display_stream_pixels(const uint8_t*, const uint16_t*, int, int) {}
static inline void display_stream_pixels_full(const uint8_t*, const uint16_t*, int, int) {}

// 5. display_text_at_nobg — рисуем шрифт 8×8 прямо в EMU_FB
static inline void display_text_at_nobg(const char* s, int x, int y, int scale, uint16_t color) {
    (void)scale;
    for (int ci = 0; s[ci]; ci++) {
        unsigned char ch = (unsigned char)s[ci];
        if (ch < 0x20 || ch > 0x7F) ch = '.';
        const uint8_t* glyph = font8x8[ch - 0x20];
        for (int row = 0; row < 8; row++) {
            int py = y + row;
            if (py < 0 || py >= POFO_H) continue;
            for (int col = 0; col < 8; col++) {
                int px = x + ci * 8 + col;
                if (px < 0 || px >= POFO_W) continue;
                if (glyph[row] & (0x80 >> col))
                    POFO_FB[py * POFO_W + px] = color;
            }
        }
    }
}

// 6. display_text_center_nobg
static inline void display_text_center_nobg(const char* s, int y, int scale, uint16_t color) {
    int len = 0; while (s[len]) len++;
    int w = len * 8;
    int x = (POFO_W - w) / 2;
    if (x < 0) x = 0;
    display_text_at_nobg(s, x, y, scale, color);
}

// 7. joypad_buttons: 0 = pressed, NES bit order
static inline uint8_t joypad_buttons(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint8_t pad = 0xFF;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) pad &= ~0x10;
        if (sc == 81) pad &= ~0x20;
        if (sc == 80) pad &= ~0x40;
        if (sc == 79) pad &= ~0x80;
        if (sc == 29) pad &= ~0x01;
        if (sc == 27) pad &= ~0x02;
        if (sc == 22) pad &= ~0x04;
        if (sc == 40) pad &= ~0x08;
    }
    return pad;
}

// 8. UART
#define uart0 ((void*)1)
static inline int uart_is_readable(void*) { return uart_rx_ready(); }
static inline int uart_getc(void*) { return uart_getc(); }

// 9. time_us_32
static inline uint32_t time_us_32(void) { return h3_hs_timer_lo_us(); }

// 10. putchar — для printf в C++ системе
extern "C" int putchar(int c);

// 11. display_flush — no-op на H3 (fb_flush вызывается после scale)
static inline void display_flush(void) {}

// 11. display_set_pixel — не используется, но пусть будет
static inline void display_set_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < POFO_W && y >= 0 && y < POFO_H)
        POFO_FB[y * POFO_W + x] = color;
}

#endif