// vecx_host.c — host-слой GCE Vectrex (vecx/libretro) для H3 bare-metal.
//
// Ядро: h3_bare/cores/vecx/ (vecx.c + e6809.c + vecx_psg.c) — без libretro.
// Жизненный цикл:
//   vecx_init_game(rom, size) — BIOS (вшит) + ROM в cart, vecx_reset
//   vecx_run_frame()           — vecx_emu() до нового кадра (osint_render)
//   vecx_host_render()         — растеризация векторов vectors_draw[] в EMU_FB
//
// Ввод: USB-клавиатура (ремап REMAP_PLAT_VECTREX) + Sega-геймпад
//   (стик 4 направлений + 4 кнопки: A=1, B=2, X=3, Y=4).
// Выход: ESC (удержание ~0.8 с).

#include <stdint.h>
#include <string.h>

#include "vecx/vecx.h"
#include "vecx/e6809.h"
#include "vecx/vecx_psg.h"
#include "vecx/osint.h"
#include "vecx/bios/system.h"   // bios_data[] + bios_data_size (вшитый BIOS)

#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "emu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

// Указатели ядра (объявлены в vecx.c)
extern unsigned char cart[65536];

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// Размер растрового экрана Vectrex (как libretro.c: WIDTH/HEIGHT)
#define VX_W    330
#define VX_H    410

// Буфер кадра RGB1555 (as в libretro.c: BUFSZ = WIDTH*HEIGHT)
static uint16_t vx_fb[VX_W * VX_H] __attribute__((aligned(4)));

static int g_loaded = 0;

// ---- вставка: vecx.c объявляет rom[], cart[], но bios_data[8192] надо скопировать
// в rom[] (BA ROM). libretro.c делал memcpy(rom, bios_data, bios_data_size).
static int have_bios = 0;

// ---- Рендер: растеризация линий в vx_fb → EMU_FB ----
// Из vecx.h: vectors_draw[] — массив vector_t {x0,y0,x1,y1,color}
//   координаты: x в [-33000..33000], y в [-41000..41000]
//   color: 0..127 (яркость), >127 = invalid
static inline uint16_t vx_rgb1555(int col) {
    uint32_t c = (uint32_t)col;
    // RGB1555 из серого (VR vectrex монохромный, но libretro пропускает через 5-бит)
    c >>= 2;
    if (c > 31) c = 31;
    return (uint16_t)((c << 10) | (c << 5) | c);
}

static void vx_draw_line(long x0, long y0, long x1, long y1, int color) {
    // масштаб: ALG_MAX_X=33000, ALG_MAX_Y=41000 → VX_W/VX_H
    int X0 = (int)(((float)x0 / 33000.0f) * VX_W + VX_W/2.0f);
    int X1 = (int)(((float)x1 / 33000.0f) * VX_W + VX_W/2.0f);
    int Y0 = (int)(((float)y0 / 41000.0f) * VX_H + VX_H/2.0f);
    int Y1 = (int)(((float)y1 / 41000.0f) * VX_H + VX_H/2.0f);

    // Брезенхем
    int dx = (X1 > X0) ? (X1 - X0) : (X0 - X1), sx = X0 < X1 ? 1 : -1;
    int dy = (Y1 > Y0) ? (Y0 - Y1) : (Y1 - Y0), sy = Y0 < Y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    uint16_t col = vx_rgb1555(color);

    int guard = 0;
    for (;;) {
        if (X0 >= 0 && X0 < VX_W && Y0 >= 0 && Y0 < VX_H)
            vx_fb[Y0 * VX_W + X0] = col;
        if (X0 == X1 && Y0 == Y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; X0 += sx; }
        if (e2 < dy)  { err += dx; Y0 += sy; }
        if (++guard > VX_W + VX_H) break;
    }
}

// Нарисовать текущий список векторов в vx_fb (как osint_render без GPU)
static void vx_rasterize(void) {
    memset(vx_fb, 0, sizeof(vx_fb));
    for (long i = 0; i < vector_draw_cnt; i++) {
        unsigned char color = vectors_draw[i].color;
        if (color >= 128) continue;   // invalid
        long x0 = vectors_draw[i].x0, y0 = vectors_draw[i].y0;
        long x1 = vectors_draw[i].x1, y1 = vectors_draw[i].y1;
        if (x0 == x1 && y0 == y1) {
            // точка
            int X = (int)(((float)x0 / 33000.0f) * VX_W + VX_W/2.0f);
            int Y = (int)(((float)y0 / 41000.0f) * VX_H + VX_H/2.0f);
            if (X >= 0 && X < VX_W && Y >= 0 && Y < VX_H)
                vx_fb[Y * VX_W + X] = vx_rgb1555(color);
        } else {
            vx_draw_line(x0, y0, x1, y1, color);
        }
    }
}

// Скопировать vx_fb в EMU_FB (масштаб VX_W/VX_H → EMU_W/EMU_H)
static void vx_blit_to_fb(void) {
    for (int y = 0; y < EMU_H; y++) {
        int sy = (y * VX_H) / EMU_H;
        if (sy >= VX_H) sy = VX_H - 1;
        for (int x = 0; x < EMU_W; x++) {
            int sx = (x * VX_W) / EMU_W;
            if (sx >= VX_W) sx = VX_W - 1;
            EMU_FB[y * EMU_W + x] = vx_fb[sy * VX_W + sx];
        }
    }
}

// ---- Ввод ----
static uint8_t vx_buttons(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint8_t b = 0;
    // Стик по умолчанию в центре (128 = покой)
    alg_jch0 = 128;
    alg_jch1 = 128;
    // Клавиатура (ремап): A=1, B=2, X=3, Y=4 (биты 0..3), крестовина = стик
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_A, keys, n)) b |= 0x01;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_B, keys, n)) b |= 0x02;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_X, keys, n)) b |= 0x04;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_Y, keys, n)) b |= 0x08;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_UP, keys, n))    alg_jch1 = 0x00;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_DOWN, keys, n))  alg_jch1 = 0xff;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_LEFT, keys, n))  alg_jch0 = 0x00;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_RIGHT, keys, n)) alg_jch0 = 0xff;
    // Sega-геймпад: крестовина + A/B/X/Y = 1/2/3/4
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) alg_jch1 = 0x00;
    if (sp & 0x0002) alg_jch1 = 0xff;
    if (sp & 0x0004) alg_jch0 = 0x00;
    if (sp & 0x0008) alg_jch0 = 0xff;
    if (sp & 0x0010) b |= 0x01;   // Sega A = 1
    if (sp & 0x0020) b |= 0x02;   // Sega B = 2
    if (sp & 0x0100) b |= 0x04;   // Sega X = 3
    if (sp & 0x0200) b |= 0x08;   // Sega Y = 4
    return b;
}

// ---- Реализация osint_render() и vecx_snd_push() (архитектура ядра) ----
// Оба вызываются из vecx_emu() (см. vecx.c: osint_render на 1214, snd_push на 1230).
// osint_render() — растризует vectors_draw[] в vx_fb и блитит в EMU_FB.
void osint_render(void) {
    vx_rasterize();
    vx_blit_to_fb();
}

void vecx_snd_push(unsigned samps) {
    (void)samps;   // звук заглушен (нет DAC-вывода)
}

// ---- API (для emu.c) ----
int vecx_init_game(const uint8_t* rom, uint32_t size) {
    printf("Vectrex: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    if (!rom || size == 0) { printf("Vectrex: no rom\n"); return 0; }

    // BIOS: вшит в ядро (bios_data из system.h) — копируем в rom[8192]
    if (!have_bios) {
        extern unsigned char rom[8192];
        memcpy(rom, bios_data, bios_data_size);
        have_bios = 1;
    }

    // ROM в cart[65536]
    memset(cart, 0, sizeof(cart));
    if (size > sizeof(cart)) size = sizeof(cart);
    memcpy(cart, rom, size);
    for (int b = 0; b < (int)sizeof(cart); b++)
        set_cart(b, cart[b]);

    vecx_psg_init();
    vecx_reset();

    g_loaded = 1;
    printf("Vectrex: loaded %u bytes\n", (unsigned)size);
    return 1;
}

// Функция-помощник не нужна — rom[] объявлен в vecx.c (extern unsigned char rom[8192])
// и доступен через vecx.h.

void vecx_run_frame(void) {
    if (!g_loaded) return;

    // Ввод перед кадром
    uint8_t buttons = vx_buttons();
    vecx_psg_io_wr(buttons);

    // Центрируем стик если крестовина не тронута (как retro_run: центр = 128)
    // (vx_buttons уже выставляет при нажатии; здесь возвращаем центр, если не нажат)
    // — упрощение: полагаемся на то, что vx_buttons не трогает без нажатия.
    // Сброс аналога при отпускании:
    // (в retro_run этого нет — аналог держит последнее; но для крестовины так и надо)

    // Один кадр: 30000 циклов = 1 кадр при 1.5 МГц / ~50 Гц (как retro_run)
    // Внутри vecx_emu сам вызовет osint_render() — рендер в EMU_FB уже готов.
    vecx_emu(30000);
}

void vecx_render_frame(void) {
    // уже сделано в vecx_run_frame (osint_render внутри vecx_emu)
}

// ---- точка входа emu.c ----
void emu_run_vectrex(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (vecx_init_game(rom, size) != 1) {
        printf("Vectrex: init failed\n");
        return;
    }
    emu_set_border_color(0x00000B14);   // тёмно-синий
    uint8_t raw_keys[6];
    uint32_t esc_hold_us = 0;
    emu_throttle_reset();
    for (;;) {
        vecx_run_frame();
        emu_throttle();
        emu_scale(VX_W, VX_H);   // рендер уже в EMU_FB через vx_blit
        fb_flush();
        // ESC удержание ~0.8с — выход
        int nk = usb_kbd_get_raw(raw_keys, 6);
        int esc = 0;
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) { esc = 1; break; }
        if (esc) {
            if (!esc_hold_us) esc_hold_us = h3_hs_timer_lo_us();
            else if (h3_hs_timer_lo_us() - esc_hold_us > 800000) goto exit;
        } else {
            esc_hold_us = 0;
        }
    }
exit:
    g_loaded = 0;
    fb_clear(); fb_flush();
}