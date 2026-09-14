// h3_de2_scaler.c — аппаратный скейлер DE2 через UI-канал + GSU1.
//
// Путь как в ядре Linux (sun8i_ui_scaler): UI-канал (channel 1) читает
// маленький RGB-буфер (CFG.SIZE = вход), GSU1 (Global Scaler Unit 1,
// адрес mixer_base + 0x30000) растягивает его до OVL_SIZE (выход).
// Blender ROUTE остаётся 1 — та же труба, что и меню, поэтому риск
// чёрного экрана минимальный (в отличие от VI+VSU, где ROUTE=0).
//
// Регистры GSU1 — по sun8i_ui_scaler.h из ядра Linux:
//   CTRL 0x00, INSIZE 0x10, HSTEP 0x18, VSTEP 0x1c, HPHS 0x20, VPHS 0x24,
//   OUTSIZE 0x28, HCOEFF 0x40 (32 шт), VCOEFF 0x140 (32 шт)
#include <stdint.h>
#include <string.h>
#include "h3_de2.h"

extern int printf(const char* fmt, ...);

// GSU1 для mixer0: MUX0 + 0x30000
#define GSU_BASE        (H3_DE2_MUX0_BASE + 0x30000)

#define GSU_CTRL        0x00
#define GSU_INSIZE      0x10
#define GSU_HSTEP       0x18
#define GSU_VSTEP       0x1c
#define GSU_HPHS        0x20
#define GSU_VPHS        0x24
#define GSU_OUTSIZE     0x28
#define GSU_HCOEFF      0x40
#define GSU_VCOEFF      0x140

#define GSU_CTRL_EN         (1u << 0)
#define GSU_CTRL_COEFF_RDY  (1u << 4)
#define GSU_COEFF_COUNT     32

#define GSU_REG(off)    (*(volatile uint32_t*)(GSU_BASE + (off)))

static int g_emu_mode = 0;

// Загрузить коэффициенты GSU: для integer scale — один tap 1.0 (nearest).
// 0x40000000 = 1.0 в формате 0.30 (сдвиг на 30 бит).
static void gsu_load_coeff_nearest(uint32_t hscale, uint32_t vscale) {
    // При integer upscale (hscale == 1<<20 / N) полифаза не нужна.
    // Коэффициенты для фазы 0: weight на центральный tap.
    // Сумма тапов должна быть 1.0 → 0x40000000.
    volatile uint32_t *hc = (volatile uint32_t*)(GSU_BASE + GSU_HCOEFF);
    volatile uint32_t *vc = (volatile uint32_t*)(GSU_BASE + GSU_VCOEFF);

    for (int i = 0; i < GSU_COEFF_COUNT; i++) {
        hc[i] = 0;
        vc[i] = 0;
    }
    // Для масштабирования в целое число раз фазовая позиция первого
    // выходного пикселя = 0, вес 1.0 ставим в tap 0.
    (void)hscale; (void)vscale;
    hc[0] = 0x40000000;
    vc[0] = 0x40000000;
}

// Включить режим эмулятора: маленький RGB буфер fb_addr (src_w×src_h)
// растягивается GSU1 на окно dst_w×dst_h в позиции (dst_x, dst_y).
// format_rgb565: 1 = RGB565, 0 = XRGB8888.
int de2_set_emu_mode(int src_w, int src_h, int dst_w, int dst_h,
                     int dst_x, int dst_y, uint32_t fb_addr,
                     int format_rgb565) {
    uint32_t fmt = format_rgb565
        ? H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_RGB_565)
        : H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
    int bpp = format_rgb565 ? 2 : 4;

    printf("DE2: emu_mode src=%dx%d dst=%dx%d pos=%d,%d fb=0x%X fmt=%s\n",
           src_w, src_h, dst_w, dst_h, dst_x, dst_y, fb_addr,
           format_rgb565 ? "RGB565" : "XRGB8888");
    printf("DE2: GSU1_BASE=0x%X\n", GSU_BASE);

    // UI-канал (channel 1): вход = маленький буфер, выход = окно на экране.
    H3_DE2_MUX0_UI->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN | fmt;
    H3_DE2_MUX0_UI->CFG[0].SIZE = H3_DE2_WH(src_w, src_h);
    H3_DE2_MUX0_UI->CFG[0].COORD = ((uint32_t)dst_y << 16) | (uint32_t)dst_x;
    H3_DE2_MUX0_UI->CFG[0].PITCH = bpp * src_w;
    H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = fb_addr;
    H3_DE2_MUX0_UI->OVL_SIZE = H3_DE2_WH(dst_w, dst_h);
    printf("DE2: UI ATTR=0x%X SIZE=0x%X COORD=0x%X PITCH=%d LADDR=0x%X OVL=0x%X\n",
           H3_DE2_MUX0_UI->CFG[0].ATTR, H3_DE2_MUX0_UI->CFG[0].SIZE,
           H3_DE2_MUX0_UI->CFG[0].COORD, H3_DE2_MUX0_UI->CFG[0].PITCH,
           H3_DE2_MUX0_UI->CFG[0].TOP_LADDR, H3_DE2_MUX0_UI->OVL_SIZE);

    // GSU1: включить только на время настройки
    GSU_REG(GSU_CTRL) = 0;

    uint32_t hstep = ((uint32_t)src_w << 20) / (uint32_t)dst_w;
    uint32_t vstep = ((uint32_t)src_h << 20) / (uint32_t)dst_h;
    uint32_t insize = H3_DE2_WH(src_w, src_h);
    uint32_t outsize = H3_DE2_WH(dst_w, dst_h);

    GSU_REG(GSU_INSIZE)  = insize;
    GSU_REG(GSU_HSTEP)   = hstep;
    GSU_REG(GSU_VSTEP)   = vstep;
    GSU_REG(GSU_HPHS)    = 0;
    GSU_REG(GSU_VPHS)    = 0;
    GSU_REG(GSU_OUTSIZE) = outsize;

    gsu_load_coeff_nearest(hstep, vstep);

    printf("DE2: GSU INS=0x%X HS=0x%X VS=0x%X OUTS=0x%X\n",
           GSU_REG(GSU_INSIZE), GSU_REG(GSU_HSTEP),
           GSU_REG(GSU_VSTEP), GSU_REG(GSU_OUTSIZE));

    GSU_REG(GSU_CTRL) = GSU_CTRL_EN | GSU_CTRL_COEFF_RDY;
    printf("DE2: GSU enabled CTRL=0x%X\n", GSU_REG(GSU_CTRL));

    // Apply
    H3_DE2_MUX0_GLB->DBUFFER = 1;
    printf("DE2: applied\n");

    g_emu_mode = 1;
    return 0;
}

// Вернуться в UI-режим (меню): фреймбуфер 1024×600 XRGB8888, GSU выкл.
void de2_set_ui_mode(uint32_t fb_addr) {
    uint32_t screen_size = H3_DE2_WH(1024, 600);

    GSU_REG(GSU_CTRL) = 0;

    H3_DE2_MUX0_UI->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN |
        H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
    H3_DE2_MUX0_UI->CFG[0].SIZE = screen_size;
    H3_DE2_MUX0_UI->CFG[0].COORD = 0;
    H3_DE2_MUX0_UI->CFG[0].PITCH = 4 * 1024;
    H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = fb_addr;
    H3_DE2_MUX0_UI->OVL_SIZE = screen_size;

    H3_DE2_MUX0_GLB->DBUFFER = 1;

    g_emu_mode = 0;
}

int de2_is_emu_mode(void) { return g_emu_mode; }

// Тест: залить RGB565-буфер 320×240 градиентом, растянуть GSU1 в 640×480.
void de2_scale_test(void) {
    uint16_t *fb = (uint16_t*)0x5F800000;
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            uint16_t r = (x * 32) / 320;
            uint16_t g = (y * 64) / 240;
            uint16_t b = 16;
            fb[y * 320 + x] = (r << 11) | (g << 5) | b;
        }
    }
    printf("DE2: test fb filled\n");
    de2_set_emu_mode(320, 240, 640, 480, 192, 60, 0x5F800000, 1);
}