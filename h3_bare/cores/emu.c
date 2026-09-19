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
// Оптимизация: без делений в пиксельном цикле. Таблица sx[] (индекс исходной
// колонки для каждой целевой) считается один раз — на Cortex-A7 деление ~12-20
// тактов, а при 500К+ пикселей это съедало 5-10 мс/кадр.
void emu_scale(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;
    int dst_w = (src_w * FB_H) / src_h;
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_h = FB_H;
    int dst_x = (FB_W - dst_w) / 2;
    uint32_t* dst = (uint32_t*)FB_ADDR;

    // Предрасчёт исходной колонки для каждой целевой (nearest neighbour):
    // аккумулятор 16.16 — каждая итерация добавляет src_w/dst_w, обёртка
    // по src_w (амплитуда = src_w, шаг = src_w/dst_w).
    static int sx_tab[FB_W];
    uint32_t step_x = ((uint32_t)src_w << 16) / (uint32_t)dst_w;
    uint32_t acc_x = step_x >> 1;   // округление к ближайшему
    for (int dx = 0; dx < dst_w; dx++) {
        sx_tab[dx] = (int)(acc_x >> 16);
        acc_x += step_x;
        if (acc_x >= ((uint32_t)src_w << 16)) acc_x -= (uint32_t)src_w << 16;
    }

    // левое/правое поле
    if (g_border_color && dst_x > 0) {
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = 0; dx < dst_x; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }
    if (g_border_color) {
        int right = dst_x + dst_w;
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = right; dx < FB_W; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }

    // Построковый проход: исходная строка = аккумулятор 16.16 => без `/`
    uint32_t step_y = ((uint32_t)src_h << 16) / (uint32_t)dst_h;
    uint32_t y_acc = step_y >> 1;              // округление к ближайшей строке
    int sy = 0;
    for (int dy = 0; dy < dst_h; dy++) {
        const uint16_t* src = EMU_FB + sy * EMU_W;
        uint32_t* d = dst + (uint32_t)dy * FB_W + (uint32_t)dst_x;
        for (int dx = 0; dx < dst_w; dx++) {
            uint16_t p = src[sx_tab[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            d[dx] = (r << 16) | (g << 8) | b;
        }
        y_acc += step_y;
        int nsy = (int)(y_acc >> 16);
        if (nsy > sy) { if (nsy >= src_h) nsy = src_h - 1; sy = nsy; }
    }
}

// ---- integer scale (целочисленный множитель) для портативных систем ----
// Картинка чёткая: один пиксель исходника = N×N пикселей экрана, поля по
// бокам (снизу/сверху). Множитель — максимальный, влезающий в 1024×600.
// Формат на выходе — RGB565 → XRGB8888 в FB_ADDR (как emu_scale).
void emu_scale_int(int src_w, int src_h) {
    // Выбираем множитель: min(FB_W/src_w, FB_H/src_h), целый
    int mul = FB_W / src_w;
    int mh  = FB_H / src_h;
    if (mh < mul) mul = mh;
    if (mul < 1) { emu_scale(src_w, src_h); return; }  // падаем на stretch

    int dst_w = src_w * mul;
    int dst_h = src_h * mul;
    int dst_x = (FB_W - dst_w) / 2;
    int dst_y = (FB_H - dst_h) / 2;
    uint32_t* dst = (uint32_t*)FB_ADDR;

    // Поля (сверху/снизу/слева/справа)
    if (g_border_color) {
        // верх
        for (int y = 0; y < dst_y; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // низ
        for (int y = dst_y + dst_h; y < FB_H; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // левая/правая полоса (в зоне картинки)
        for (int y = dst_y; y < dst_y + dst_h; y++) {
            for (int x = 0; x < dst_x; x++)
                dst[y * FB_W + x] = g_border_color;
            for (int x = dst_x + dst_w; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        }
    }

    // Масштабирование: каждый исходный пиксель — блок mul×mul
    for (int sy = 0; sy < src_h; sy++) {
        const uint16_t* src = EMU_FB + sy * EMU_W;
        int dy0 = dst_y + sy * mul;
        for (int sx = 0; sx < src_w; sx++) {
            uint16_t p = src[sx];
            uint32_t c = (((p >> 11) & 0x1F) << 3) << 16
                       | (((p >> 5) & 0x3F) << 2) << 8
                       | ((p & 0x1F) << 3);
            int dx0 = dst_x + sx * mul;
            for (int dy = 0; dy < mul; dy++)
                for (int dx = 0; dx < mul; dx++)
                    dst[(dy0 + dy) * FB_W + dx0 + dx] = c;
        }
    }
}

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
}

// ---- throttle ----
#include "settings.h"
#include "i2s.h"
static uint32_t emu_ts0 = 0;
void emu_throttle(void) {
    // Мигаем светодиодом: видно, что код жив и кадры идут
    static uint32_t led_fc = 0;
    if ((++led_fc & 0x1F) == 0) led_set(led_fc & 0x20);
    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;
    uint32_t elapsed = now - emu_ts0;
    // Остаток кадра: вместо голого delay КРУТИМ i2s_flush() — звук из
    // кольца выталкивается в I2S FIFO непрерывно в реальном времени.
    // Иначе FIFO (32 пары) пустеет за ~0.7 мс и молчит 15 мс — хрип/треск.
    if (elapsed < emu_period_us) {
        uint32_t target = emu_ts0 + emu_period_us;
        while ((int32_t)(h3_hs_timer_lo_us() - target) < 0)
            i2s_flush();
    }
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
extern int gg_init_game(const uint8_t* rom, uint32_t size);
extern void gg_run_frame(void);
extern int portfolio_init_game(const uint8_t* rom, uint32_t size);
extern void portfolio_run_frame(void);
extern int portfolio_exit_requested(void);
extern int gb_init_game(const uint8_t* rom, uint32_t size);
extern void gb_run_frame(void);
extern void gb_render_frame(void);
extern int gba_init_game(const uint8_t* rom, uint32_t size);
extern void gba_run_frame(void);
extern void gba_render_frame(void);
extern int lynx_init_game(const uint8_t* rom, uint32_t size);
extern void lynx_run_frame(void);
extern void lynx_render_frame(void);
extern int ngp_init_game(const uint8_t* rom, uint32_t size);
extern void ngp_run_frame(void);
extern void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name);

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
        emu_throttle();
        emu_scale(256, 192);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_gg(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (gg_init_game(rom, size) != 1) {
        printf("GG: init failed\n"); return;
    }
    printf("GG: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00082030);   // тёмно-синий (GG)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        gg_run_frame();
        emu_throttle();
        emu_scale_int(160, 144);
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
        emu_scale_int(160, 144);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_gba(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (gba_init_game(rom, size) != 1) {
        printf("GBA: init failed\n"); return;
    }
    printf("GBA: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A2E);   // тёмно-синий (GBA)
    uint8_t raw_keys[6];
    uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        gba_run_frame();
        gba_render_frame();
        emu_throttle();
        emu_scale_int(240, 160);
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
        emu_scale_int(160, 102);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}

void emu_run_ngp(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb(); fb_clear(); fb_flush();
    if (ngp_init_game(rom, size) != 1) {
        printf("NGP: init failed\n"); return;
    }
    printf("NGP: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A2B);   // тёмно-синий (NGP)
    uint8_t raw_keys[6]; uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        ngp_run_frame();
        emu_throttle();
        emu_scale_int(160, 152);
        fb_flush();
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++) if (raw_keys[i] == 41) goto exit;
    }
exit: fb_clear(); fb_flush();
}