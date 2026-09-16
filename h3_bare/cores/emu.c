#include <stdint.h>
#include <string.h>
#include "emu.h"
#include "fb_text.h"
#include "uart.h"
#include "usb_kbd.h"
#include "h3_hs_timer.h"
#include "led.h"

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#define FB_ADDR 0x5F900000
#define FB_W    1024
#define FB_H    600

static uint32_t g_border_color = 0;   // цвет полей (XRGB8888), 0 = чёрный

void emu_set_border_color(uint32_t rgb888) {
    g_border_color = rgb888;
}

// ---- единый nearest-neighbour скейлер ----
void emu_scale(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;
    int dst_w = (src_w * FB_H) / src_h;
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_h = FB_H;
    int dst_x = (FB_W - dst_w) / 2;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;

    // левое поле
    if (g_border_color && dst_x > 0) {
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = 0; dx < dst_x; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }
    // правое поле
    if (g_border_color) {
        int right = dst_x + dst_w;
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = right; dx < FB_W; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * src_h) / dst_h;
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * src_w) / dst_w;
            uint16_t p = EMU_FB[sy * EMU_W + sx];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            dst[dy * FB_W + dst_x + dx] = (r << 16) | (g << 8) | b;
        }
    }
}

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
}

// ---- throttle ----
#include "settings.h"
static uint32_t emu_ts0 = 0;
void emu_throttle(void) {
    // Мигаем светодиодом: видно, что код жив и кадры идут
    static uint32_t led_fc = 0;
    if ((++led_fc & 0x1F) == 0) led_set(led_fc & 0x20);
    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;
    uint32_t elapsed = now - emu_ts0;
    if (elapsed < emu_period_us)
        h3_hs_timer_delay((emu_period_us - elapsed) * 100);
    emu_ts0 = h3_hs_timer_lo_us();
}

void emu_throttle_reset(void) {
    emu_ts0 = 0;
}

// ---- эмуляторы ----
extern void atari2600_init(const uint8_t* rom, uint32_t size);
extern void atari2600_run_frame(void);
extern void atari2600_set_difficulty(int p1_expert);
extern int a7800_init_game(const uint8_t* rom, uint32_t size);
extern void a7800_run_frame(void);
extern int a5200_init_game(const uint8_t* rom, uint32_t size);
extern void a5200_run_frame(void);
extern int sms_init_game(const uint8_t* rom, uint32_t size);
extern void sms_run_frame(void);
extern void sms_render_frame(void);
extern int portfolio_init_game(const uint8_t* rom, uint32_t size);
extern void portfolio_run_frame(void);
extern int portfolio_exit_requested(void);
extern int gb_init_game(const uint8_t* rom, uint32_t size);
extern void gb_run_frame(void);
extern void gb_render_frame(void);
extern int lynx_init_game(const uint8_t* rom, uint32_t size);
extern void lynx_run_frame(void);
extern void lynx_render_frame(void);
extern void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (a7800_init_game(rom, size) != 1) {
        printf("A7800: init failed\n"); return;
    }
    printf("A7800: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00281206);   // тёмно-бордовый
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        a7800_run_frame(); emu_throttle(); emu_scale(320, 240); fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (a5200_init_game(rom, size) != 1) {
        printf("A5200: init failed\n"); return;
    }
    printf("A5200: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00061428);   // тёмно-синий
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        a5200_run_frame();
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (sms_init_game(rom, size) != 1) {
        printf("SMS: init failed\n"); return;
    }
    printf("SMS: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00081430);   // тёмно-синий (SMS)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        sms_run_frame();
        // sms_run_frame() внутри system_gpgx_h3.c уже вызывает
        // gpgx_render_emu(256,192) — повторный sms_render_frame() не нужен
        emu_throttle();
        emu_scale(256, 192);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    atari2600_init(rom, size);
    atari2600_set_difficulty(a2600_diff_expert);
    printf("MCUME: \"%s\" size=%d diff=%s\n", rom_name ? rom_name : "?", (int)size,
           a2600_diff_expert ? "Expert" : "Novice");
    emu_set_border_color(0x00201A08);   // тёмно-янтарный (woodgrain A2600)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        atari2600_run_frame(); emu_throttle(); emu_scale(160, 192); fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_portfolio(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (portfolio_init_game(rom, size) != 1) {
        printf("Portfolio: init failed\n"); return;
    }
    printf("Portfolio: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00101816);   // тёмно-оливковый
    uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        portfolio_run_frame();
        if (portfolio_exit_requested()) break;
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        fc++;
    }
    fb_clear(); fb_flush();
}

void emu_run_gameboy(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (gb_init_game(rom, size) != 1) {
        printf("GameBoy: init failed\n"); return;
    }
    printf("GameBoy: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A0E);   // тёмно-зелёный (DMG)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        gb_run_frame();
        gb_render_frame();
        emu_throttle();
        emu_scale(160, 144);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_lynx(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (lynx_init_game(rom, size) != 1) {
        printf("Lynx: init failed\n"); return;
    }
    printf("Lynx: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E0D26);   // тёмно-фиолетовый (Lynx)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        lynx_run_frame();
        lynx_render_frame();
        emu_throttle();
        emu_scale(160, 102);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}