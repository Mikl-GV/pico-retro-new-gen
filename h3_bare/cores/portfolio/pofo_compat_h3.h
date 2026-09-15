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

// HDMI-фреймбуфер для рендера
#define POFO_HMIDI_FB  ((volatile uint32_t*)0x5F900000)
#define POFO_FB_W      1024
#define POFO_FB_H      600
// Портфолио рисует в 320×240 — смещаем в центр
#define POFO_OFX       ((POFO_FB_W - 320) / 2)
#define POFO_OFY       ((POFO_FB_H - 240) / 2)

// 1. display_fill: залить всё окно 320×240 в центре
static inline void display_fill(uint16_t color) {
    uint32_t c = (((color >> 11) & 0x1F) << 3) << 16 |
                 (((color >> 5) & 0x3F) << 2) << 8 |
                 ((color & 0x1F) << 3);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            POFO_HMIDI_FB[(POFO_OFY + y) * POFO_FB_W + (POFO_OFX + x)] = c;
}

// 2. display_fill_rect
static inline void display_fill_rect(int x0, int y0, int w, int h, uint16_t color) {
    uint32_t c = (((color >> 11) & 0x1F) << 3) << 16 |
                 (((color >> 5) & 0x3F) << 2) << 8 |
                 ((color & 0x1F) << 3);
    for (int y = y0; y < y0 + h && y < 240; y++) {
        if (y < 0) continue;
        for (int x = x0; x < x0 + w && x < 320; x++) {
            if (x < 0) continue;
            POFO_HMIDI_FB[(POFO_OFY + y) * POFO_FB_W + (POFO_OFX + x)] = c;
        }
    }
}

// 3. display_stream_begin / pixels16 / end — для LCD-строки
static inline void display_stream_begin(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
static inline void display_stream_pixels16(const uint16_t* src, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int x = 0; x < w; x++) {
            uint16_t p = src[x + dy * w];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            // y — текущая строка экрана, передана через begin (но мы игнорируем begin)
            // Используем curry_y из статической переменной, установленной stream_begin
            // НУЖЕН КОНТЕКСТ. См. ниже: pofo_stream_line задаёт gl_y.
        }
}
static inline void display_stream_end(void) {}

// Для stream нужен контекст строки — pofo_stream_line устанавливает `g_stream_y`
// перед вызовом display_stream_begin/pixels16/end. Это единственный поток.
static int g_stream_y = 0;
// Переопределяем макросы для Portfolio: stream_begin запоминает y, pixels16 пишет
#undef display_stream_begin
#undef display_stream_pixels16
#undef display_stream_end
#define display_stream_begin(x, y, w, h) do { g_stream_y = (y); } while(0)
#define display_stream_pixels16(src, w, h) do { \
    int sy_ = g_stream_y; \
    for (int dx_ = 0; dx_ < (w); dx_++) { \
        uint16_t p_ = (src)[dx_]; \
        uint32_t r_ = ((p_ >> 11) & 0x1F) << 3; \
        uint32_t g_ = ((p_ >> 5) & 0x3F) << 2; \
        uint32_t b_ = (p_ & 0x1F) << 3; \
        POFO_HMIDI_FB[(POFO_OFY + sy_) * POFO_FB_W + (POFO_OFX + dx_)] = (r_ << 16) | (g_ << 8) | b_; \
    } \
} while(0)
#define display_stream_end() do {} while(0)

// 4. display_stream_pixels — не используется Portfolio, но объявим пустым
static inline void display_stream_pixels(const uint8_t*, const uint16_t*, int, int) {}
static inline void display_stream_pixels_full(const uint8_t*, const uint16_t*, int, int) {}

// 5. display_text_at_nobg — через fb_text
static inline void display_text_at_nobg(const char* s, int x, int y, int scale, uint16_t color) {
    uint32_t c = (((color >> 11) & 0x1F) << 3) << 16 |
                 (((color >> 5) & 0x3F) << 2) << 8 |
                 ((color & 0x1F) << 3);
    fb_puts_s(POFO_OFX + x, POFO_OFY + y, s, scale, c);
}

// 6. display_text_center_nobg
static inline void display_text_center_nobg(const char* s, int y, int scale, uint16_t color) {
    uint32_t c = (((color >> 11) & 0x1F) << 3) << 16 |
                 (((color >> 5) & 0x3F) << 2) << 8 |
                 ((color & 0x1F) << 3);
    int len = 0; while (s[len]) len++;
    int w = len * (8 * scale + 2 * scale) - 2 * scale;
    int x = (320 - w) / 2;
    if (x < 0) x = 0;
    fb_puts_s(POFO_OFX + x, POFO_OFY + y, s, scale, c);
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
    uint32_t c = (((color >> 11) & 0x1F) << 3) << 16 |
                 (((color >> 5) & 0x3F) << 2) << 8 |
                 ((color & 0x1F) << 3);
    POFO_HMIDI_FB[(POFO_OFY + y) * POFO_FB_W + (POFO_OFX + x)] = c;
}

#endif