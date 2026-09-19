// vecx_host.c — host-слой GCE Vectrex (vecx/libretro) для H3 bare-metal.
//
// Ядро: h3_bare/cores/vecx/ (vecx.c + e6809.c + vecx_psg.c) — без libretro.
// Жизненный цикл:
//   vecx_init_game(rom, size) — BIOS (вшит) + ROM в cart, vecx_reset
//   vecx_run_frame()           — vecx_emu() до нового кадра; ядро само
//                                вызывает osint_render() (см. ниже)
//   vx_render_hdmi()           — растеризация vx_fb[330×410] в HDMI 1024×600
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
#include "i2s.h"

extern int printf(const char* fmt, ...);

// cart[] объявлен в vecx.c (ром загружается сюда через set_cart)
extern unsigned char cart[65536];

// Размер растрового экрана Vectrex (как libretro.c: WIDTH/HEIGHT).
// Экран ПОРТРЕТНЫЙ: 330 ширина × 410 высота.
#define VX_W    330
#define VX_H    410

// Буфер кадра RGB1555 (как в libretro.c: BUFSZ = WIDTH*HEIGHT)
static uint16_t vx_fb[VX_W * VX_H] __attribute__((aligned(4)));

// Буферы звука (для vecx_snd_push): ядро ожидает от vecx_psg_set_buffer
// и vecx_dac_set_buffer. Размер как в эталоне libretro.c (SIZE_ABUF=3800):
// psg вызывает exec каждые 8 циклов — при MPU=1.5M кадр 30000 циклов
// даёт до 3750 сэмплов. 2048 НЕ хватало → переполнение буферов и мусор
// в vx_fb (ядра vecx.c пишут dacbuf/psgbuf и он залезал в соседние
// глобалы, в т.ч. vectors_draw). Увеличиваем до 4096.
#define SIZE_ABUF 4096
static int16_t vx_psgbuf[SIZE_ABUF];
static int16_t vx_dacbuf[SIZE_ABUF];

static int g_loaded = 0;
static int have_bios = 0;   // BIOS скопирован в rom[] один раз

// ---- Рендер: растеризация линий в vx_fb ----
// Из vecx.h: vectors_draw[] — массив vector_t {x0,y0,x1,y1,color}.
// Координаты ядра: [0..ALG_MAX_X] (33000) по X, [0..ALG_MAX_Y] (41000) по Y,
// луч стартует в центре (MAX/2, MAX/2), 0 = лево/верх растра.
// color: 0..127 (яркость), 128+ = invalid (стирание списка).
static inline uint16_t vx_rgb1555(int col) {
    uint32_t c = (uint32_t)col;
    // RGB1555 из серого: VR-монохром, все каналы = яркость
    c >>= 2;
    if (c > 31) c = 31;
    return (uint16_t)((c << 10) | (c << 5) | c);
}

static void vx_draw_line(long x0, long y0, long x1, long y1, int color) {
    // Координаты Vectrex: [0..ALG_MAX_X] по X (33000), [0..ALG_MAX_Y] по Y (41000),
    // старт луча в центре (ALG_MAX_X/2, ALG_MAX_Y/2). 0 = лево/верх растра.
    // Как в эталоне libretro osint_render: чистая пропорция [0..MAX] -> [0..размер]
    // без сдвига и без инверсии.
    int X0 = (int)(((float)x0 / (float)ALG_MAX_X) * VX_W);
    int X1 = (int)(((float)x1 / (float)ALG_MAX_X) * VX_W);
    int Y0 = (int)(((float)y0 / (float)ALG_MAX_Y) * VX_H);
    int Y1 = (int)(((float)y1 / (float)ALG_MAX_Y) * VX_H);

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
            // точка — пропорция как у линий [0..MAX] -> [0..размер]
            int X = (int)(((float)x0 / (float)ALG_MAX_X) * VX_W);
            int Y = (int)(((float)y0 / (float)ALG_MAX_Y) * VX_H);
            if (X >= 0 && X < VX_W && Y >= 0 && Y < VX_H)
                vx_fb[Y * VX_W + X] = vx_rgb1555(color);
        } else {
            vx_draw_line(x0, y0, x1, y1, color);
        }
    }
}

// ---- Прямой рендер в HDMI (портретный экран Vectrex: 330×410) ----
// EMU_FB (320×240) слишком мал для 410 строк — emu_scale(330,410) читал бы
// за пределы. Рендерим vx_fb[330×410] напрямую в FB_ADDR (1024×600 XRGB8888),
// масштаб: высота 410 -> 600, ширина 330 -> ~483, центрировано, поля по бокам.
#define VX_FB_ADDR 0x5F900000
#define VX_FB_W    1024
#define VX_FB_H    600

static void vx_render_hdmi(uint32_t border) {
    uint32_t* dst = (uint32_t*)VX_FB_ADDR;
    int dst_w = (VX_W * VX_FB_H) / VX_H;   // 330*600/410 = 483
    if (dst_w > VX_FB_W) dst_w = VX_FB_W;
    int dst_x = (VX_FB_W - dst_w) / 2;

    // поля слева/справа
    if (border) {
        for (int dy = 0; dy < VX_FB_H; dy++) {
            for (int dx = 0; dx < dst_x; dx++) dst[dy * VX_FB_W + dx] = border;
            int r = dst_x + dst_w;
            for (int dx = r; dx < VX_FB_W; dx++) dst[dy * VX_FB_W + dx] = border;
        }
    }

    // построковый масштаб: источник vx_fb (RGB1555 серая), приёмник XRGB8888
    uint32_t step_y = ((uint32_t)VX_H << 16) / (uint32_t)VX_FB_H;
    uint32_t y_acc = step_y >> 1;
    int sy = 0;
    for (int dy = 0; dy < VX_FB_H; dy++) {
        const uint16_t* src = vx_fb + sy * VX_W;
        uint32_t* d = dst + (uint32_t)dy * VX_FB_W + (uint32_t)dst_x;
        uint32_t step_x = ((uint32_t)VX_W << 16) / (uint32_t)dst_w;
        uint32_t x_acc = step_x >> 1;
        for (int dx = 0; dx < dst_w; dx++) {
            uint32_t si = x_acc >> 16;
            if (si >= VX_W) si = VX_W - 1;   // защита индекса от выхода за vx_fb
            uint16_t p = src[si];
            // RGB1555: R=бит14-10, G=9-5, B=4-0 (у нас серое c==R==G==B)
            uint32_t g = ((p >> 5) & 0x1F) << 3;
            d[dx] = (g << 16) | (g << 8) | g;
            x_acc += step_x;
            if (x_acc >= ((uint32_t)VX_W << 16)) x_acc -= (uint32_t)VX_W << 16;
        }
        y_acc += step_y;
        int nsy = (int)(y_acc >> 16);
        if (nsy > sy) { if (nsy >= VX_H) nsy = VX_H - 1; sy = nsy; }
    }
}

// ---- Реализация osint_render() и vecx_snd_push() (архитектура ядра) ----
// Оба вызываются из vecx_emu() (см. vecx.c: osint_render на 1214, snd_push на 1230).
// osint_render() — только растеризует векторы в vx_fb. Вывод в HDMI делает
// vx_render_hdmi() в emu_run_vectrex (после полного кадра).
void osint_render(void) {
    vx_rasterize();
}

void vecx_snd_push(unsigned samps) {
    // Звук Vectrex: PSG (SN76489) + DAC смешиваются. Наши буферы
    // заданы через vecx_psg_set_buffer/vecx_dac_set_buffer (vx_psgbuf/vx_dacbuf).
    // Ядро заполняет их, здесь конвертируем в моно-стерео и шлём в I2S.
    if (samps > SIZE_ABUF) samps = SIZE_ABUF;
    for (unsigned i = 0; i < samps; i++) {
        int32_t v = (int32_t)vx_psgbuf[i] + (int32_t)vx_dacbuf[i];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        int16_t s = (int16_t)v;
        i2s_push_sample(s, s);
    }
}

// ---- Ввод ----
// Кнопки PSG-порта — АКТИВНЫЙ НИЗ (как retro_run в libretro.c):
//   buttons = 0xff, нажатие сбрасывает бит (A=1, B=2, X=4, Y=8).
// Аналоговый стик: alg_jch0=X (LEFT=0x00, RIGHT=0xff), alg_jch1=Y
//   (UP=0xff, DOWN=0x00), центр = 128.
static uint8_t vx_buttons(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint8_t b = 0xff;   // активный низ: все кнопки отпущены
    // Стик по умолчанию в центре (128 = покой)
    alg_jch0 = 128;
    alg_jch1 = 128;
    // Клавиатура (ремап): A=1, B=2, X=3, Y=4 (биты 0..3), крестовина = стик
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_A, keys, n)) b &= ~0x01;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_B, keys, n)) b &= ~0x02;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_X, keys, n)) b &= ~0x04;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_Y, keys, n)) b &= ~0x08;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_UP, keys, n))    alg_jch1 = 0xff;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_DOWN, keys, n))  alg_jch1 = 0x00;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_LEFT, keys, n))  alg_jch0 = 0x00;
    if (remap_kbd_pressed(REMAP_PLAT_VECTREX, BTN_RIGHT, keys, n)) alg_jch0 = 0xff;
    // Sega-геймпад: крестовина + A/B/X/Y = 1/2/3/4
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) alg_jch1 = 0xff;   // Up
    if (sp & 0x0002) alg_jch1 = 0x00;   // Down
    if (sp & 0x0004) alg_jch0 = 0x00;   // Left
    if (sp & 0x0008) alg_jch0 = 0xff;   // Right
    if (sp & 0x0010) b &= ~0x01;        // Sega A = 1
    if (sp & 0x0020) b &= ~0x02;        // Sega B = 2
    if (sp & 0x0100) b &= ~0x04;        // Sega X = 3
    if (sp & 0x0200) b &= ~0x08;        // Sega Y = 4
    return b;
}

// ---- API (для emu.c) ----
int vecx_init_game(const uint8_t* rom_data, uint32_t size) {
    printf("Vectrex: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    if (!rom_data || size == 0) { printf("Vectrex: no rom\n"); return 0; }

    // BIOS: вшит в ядро (bios_data из system.h) — копируем в rom[8192]
    if (!have_bios) {
        extern unsigned char rom[8192];
        memcpy(rom, bios_data, bios_data_size);
        have_bios = 1;
    }

    // ROM игры в cart[65536]
    memset(cart, 0, sizeof(cart));
    if (size > sizeof(cart)) size = sizeof(cart);
    memcpy(cart, rom_data, size);   // rom_data — параметр, не путать с глобалом rom!
    for (int b = 0; b < (int)sizeof(cart); b++)
        set_cart(b, cart[b]);

    vecx_psg_init();

    // Буферы звука для PSG + DAC (ядро пишет сэмплы сюда, vecx_snd_push читает)
    vecx_psg_set_buffer(vx_psgbuf);
    vecx_dac_set_buffer(vx_dacbuf);

    vecx_reset();

    g_loaded = 1;
    printf("Vectrex: loaded %u bytes\n", (unsigned)size);
    return 1;
}

void vecx_run_frame(void) {
    if (!g_loaded) return;

    // Ввод перед кадром
    uint8_t buttons = vx_buttons();
    vecx_psg_io_wr(buttons);

    // Один кадр: 30000 циклов = 1 кадр при 1.5 МГц / ~50 Гц (как retro_run)
    // Внутри vecx_emu сам вызовет osint_render() — векторы в vx_fb готовы.
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
    uint32_t border = 0x000B1430;       // XRGB − тёмно-синий для полей Vectrex
    uint8_t raw_keys[6];
    uint32_t esc_hold_us = 0;
    emu_throttle_reset();
    for (;;) {
        vecx_run_frame();                // osint_render внутри рисует в vx_fb
        emu_throttle();
        vx_render_hdmi(border);          // vx_fb[330×410] → HDMI 1024×600 (портрет)
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