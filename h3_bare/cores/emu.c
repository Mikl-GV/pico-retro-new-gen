#include <stdint.h>
#include <string.h>
#include "emu.h"
#include "fb_text.h"
#include "uart.h"
#include "usb_kbd.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#define FB_ADDR 0x5F900000
#define FB_W    1024
#define FB_H    600

// ---- единый nearest-neighbour скейлер ----
// src_w/h — родное разрешение (в левом верхнем углу EMU_FB)
// scale — целый множитель
void emu_scale(int src_w, int src_h, int scale) {
    int dst_w = src_w * scale;
    int dst_h = src_h * scale;
    int dst_x = (FB_W - dst_w) / 2;
    int dst_y = (FB_H - dst_h) / 2;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;

    for (int sy = 0; sy < src_h; sy++) {
        uint16_t* row = EMU_FB + sy * EMU_W;
        for (int dy = 0; dy < scale; dy++) {
            int py = dst_y + sy * scale + dy;
            for (int sx = 0; sx < src_w; sx++) {
                uint16_t p = row[sx];
                uint32_t r = ((p >> 11) & 0x1F) << 3;
                uint32_t g = ((p >> 5) & 0x3F) << 2;
                uint32_t b = (p & 0x1F) << 3;
                uint32_t px = (r << 16) | (g << 8) | b;
                for (int dx = 0; dx < scale; dx++)
                    dst[py * FB_W + dst_x + sx * scale + dx] = px;
            }
        }
    }
}

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
}

// ---- throttle 60 FPS ----
static uint32_t emu_ts0 = 0;
void emu_throttle(void) {
    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;
    uint32_t elapsed = now - emu_ts0;
    if (elapsed < 16667)
        h3_hs_timer_delay((16667 - elapsed) * 100);
    emu_ts0 = h3_hs_timer_lo_us();
}

// ---- эмуляторы ----

extern void atari2600_init(const uint8_t* rom, uint32_t size);
extern void atari2600_run_frame(void);
extern int a7800_init_game(const uint8_t* rom, uint32_t size);
extern void a7800_run_frame(void);
extern int a5200_init_game(const uint8_t* rom, uint32_t size);
extern void a5200_run_frame(void);
extern int sms_init_game(const uint8_t* rom, uint32_t size);
extern void sms_run_frame(void);
extern void sms_render_frame(void);

static void emu_wait_key(void) {
    for (;;) {
        if (uart_rx_ready()) { uart_getc(); return; }
        uint8_t keys[6];
        int n = usb_kbd_get_raw(keys, 6);
        if (n > 0) return;
    }
}

void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (a7800_init_game(rom, size) != 1) {
        printf("A7800: init failed\n"); return;
    }
    printf("A7800: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        a7800_run_frame(); emu_throttle(); emu_scale(320, 240, 2); fb_flush();
        if ((fc % 60) == 0) printf("a7800 f=%u\n", (unsigned)fc); fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
        if (uart_rx_ready()) break;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (a5200_init_game(rom, size) != 1) {
        printf("A5200: init failed\n"); return;
    }
    printf("A5200: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        a5200_run_frame(); emu_throttle(); emu_scale(320, 240, 2); fb_flush();
        if ((fc % 60) == 0) printf("a5200 f=%u\n", (unsigned)fc); fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
        if (uart_rx_ready()) break;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (sms_init_game(rom, size) != 1) {
        printf("SMS: init failed\n"); return;
    }
    printf("SMS: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        sms_run_frame(); sms_render_frame(); emu_throttle(); emu_scale(256, 192, 3); fb_flush();
        if ((fc % 60) == 0) printf("sms f=%u\n", (unsigned)fc); fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
        if (uart_rx_ready()) break;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    atari2600_init(rom, size);
    printf("MCUME: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        atari2600_run_frame(); emu_throttle(); emu_scale(160, 192, 3); fb_flush();
        if ((fc % 60) == 0) printf("mcume f=%u\n", (unsigned)fc); fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
        if (uart_rx_ready()) break;
    }
exit: fb_clear(); fb_flush();
}

// Запасное самописное ядро A2600 (не используется — rom_browser вызывает MCUME)
void emu_run_a2600(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom; (void)size; (void)rom_name;
    printf("A2600 native: not compiled (use MCUME)\n");
}