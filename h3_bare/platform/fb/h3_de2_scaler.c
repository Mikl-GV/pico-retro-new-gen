// h3_de2_scaler.c — аппаратный скейлер DE2 через VI-канал + VSU.
// Позволяет вывести маленький фреймбуфер (160×192, 256×240, 320×240)
// растянутым на большую область экрана без нагрузки на CPU.
// Если VSU не включается — вызывающий может использовать CPU-блит как fallback.
#include <stdint.h>
#include <string.h>
#include "h3_de2.h"

extern int printf(const char* fmt, ...);

// VSU register offsets (от mixer base)
#define VSU_CTRL        0x00
#define VSU_YINSIZE     0x40
#define VSU_YHPHASE     0x48
#define VSU_YVPHASE     0x4c
#define VSU_YHSTEP      0x50
#define VSU_YVSTEP      0x54
#define VSU_CINSIZE     0x60
#define VSU_CHPHASE     0x68
#define VSU_CVPHASE     0x6c
#define VSU_CHSTEP      0x70
#define VSU_CVSTEP      0x74
#define VSU_OUTSIZE     0x80

#define VSU_CTRL_EN     (1u << 0)
#define VSU_CTRL_COEFF_RDY (1u << 4)

// Адрес VSU для mixer 0: H3_DE2_MUX0_BASE + 0x20000
// H3_DE2_MUX0_BASE = H3_DE_BASE + 0x100000
#define VSU_BASE        (H3_DE_BASE + 0x100000 + 0x20000)

#define VSU_REG(off)    (*(volatile uint32_t*)(VSU_BASE + (off)))

// VI channel 0 (с масштабированием)
// Channel 0 = VI, Channel 1 = UI
#define VI_BASE         (H3_DE2_MUX0_CHAN_BASE + 0 * H3_DE2_MUX_CHAN_SZ)

static int g_emu_mode = 0; // 0=UI, 1=emu
static int g_emu_w = 0, g_emu_h = 0;

// Включить режим эмулятора: фреймбуфер fb_addr размером src_w×src_h
// растягивается в dst_w×dst_h с позицией (dst_x, dst_y) на экране.
// format_rgb565=1 → RGB565, 0 → XRGB8888.
// Возвращает 0 при успехе, -1 если что-то пошло не так.
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

    printf("DE2: VSU_BASE=0x%X VI_BASE=0x%X\n", VSU_BASE, VI_BASE);
printf("DE2: BLDR ROUTE=%u OUTSIZE=0x%X\n",
           H3_DE2_MUX0_BLD->ROUTE, H3_DE2_MUX0_BLD->OUTPUT_SIZE);

    // 1. Отключаем UI-канал (channel 1)
    H3_DE2_MUX0_UI->CFG[0].ATTR = 0;
    printf("DE2: UI disabled\n");

    // 2. Настраиваем VI-канал (channel 0)
    volatile H3_DE2_VI_TypeDef *vi = (H3_DE2_VI_TypeDef*)VI_BASE;
    memset((void*)vi, 0, sizeof(H3_DE2_VI_TypeDef));

    vi->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN | fmt;
    vi->CFG[0].SIZE = H3_DE2_WH(src_w, src_h);
    vi->CFG[0].COORD = (dst_y << 16) | dst_x;
    vi->CFG[0].PITCH[0] = bpp * src_w;
    vi->CFG[0].TOP_LADDR[0] = fb_addr;
    printf("DE2: VI ATTR=0x%X SIZE=0x%X COORD=0x%X PITCH=%d LADDR=0x%X\n",
           vi->CFG[0].ATTR, vi->CFG[0].SIZE, vi->CFG[0].COORD,
           vi->CFG[0].PITCH[0], vi->CFG[0].TOP_LADDR[0]);

    // 3. Настраиваем выходной размер для скейлера
    vi->OVL_SIZE[0] = H3_DE2_WH(dst_w, dst_h);
    printf("DE2: OVL_SIZE=0x%X\n", vi->OVL_SIZE[0]);

    // 4. Настраиваем VSU скейлер
    uint32_t hstep = ((uint32_t)src_w << 20) / (uint32_t)dst_w;
    uint32_t vstep = ((uint32_t)src_h << 20) / (uint32_t)dst_h;
    uint32_t insize = H3_DE2_WH(src_w, src_h);
    uint32_t outsize = H3_DE2_WH(dst_w, dst_h);
    printf("DE2: VSU hstep=0x%X vstep=0x%X insize=0x%X outsize=0x%X\n",
           hstep, vstep, insize, outsize);

    VSU_REG(VSU_CTRL) = 0;
    VSU_REG(VSU_YINSIZE) = insize;
    VSU_REG(VSU_YHSTEP) = hstep;
    VSU_REG(VSU_YVSTEP) = vstep;
    VSU_REG(VSU_CINSIZE) = insize;
    VSU_REG(VSU_CHSTEP) = hstep;
    VSU_REG(VSU_CVSTEP) = vstep;
    VSU_REG(VSU_OUTSIZE) = outsize;

    printf("DE2: VSU CTRL=0x%X YINS=0x%X YHS=0x%X YVS=0x%X OUTS=0x%X\n",
           VSU_REG(VSU_CTRL), VSU_REG(VSU_YINSIZE), VSU_REG(VSU_YHSTEP),
           VSU_REG(VSU_YVSTEP), VSU_REG(VSU_OUTSIZE));

    // nearest-neighbour coefficients
    volatile uint32_t *coeff = (volatile uint32_t*)(VSU_BASE + 0x200);
    for (int i = 0; i < 32 * 8; i++) coeff[i] = 0;
    coeff[0] = 0x40000000;
    coeff[32] = 0x40000000;
    coeff[64] = 0x40000000;
    coeff[96] = 0x40000000;
    coeff[128] = 0x40000000;
    coeff[160] = 0x40000000;

    VSU_REG(VSU_CTRL) = VSU_CTRL_EN | VSU_CTRL_COEFF_RDY;
    printf("DE2: VSU enabled, CTRL=0x%X\n", VSU_REG(VSU_CTRL));

    // 5. Blender route = 0 (VI)
    H3_DE2_MUX0_BLD->ROUTE = 0;
    uint32_t screen_size = H3_DE2_WH(1024, 600);
    H3_DE2_MUX0_BLD->OUTPUT_SIZE = screen_size;
    H3_DE2_MUX0_BLD->ATTR[0].INSIZE = screen_size;
    printf("DE2: BLD ROUTE=%u OUTSIZE=0x%X\n",
           H3_DE2_MUX0_BLD->ROUTE, H3_DE2_MUX0_BLD->OUTPUT_SIZE);

    H3_DE2_MUX0_GLB->DBUFFER = 1;
    printf("DE2: applied\n");

    g_emu_mode = 1;
    g_emu_w = src_w;
    g_emu_h = src_h;
    return 0;
}

// Тест DE2-скейлера: заполнить RGB565-буфер градиентом и включить VI+scale.
// Вызывается из Settings.
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

// Вернуться в UI-режим (меню): фреймбуфер 1024×600 XRGB8888
void de2_set_ui_mode(uint32_t fb_addr) {
    uint32_t screen_size = H3_DE2_WH(1024, 600);

    // Disable VSU
    VSU_REG(VSU_CTRL) = 0;
    *((volatile uint32_t*)(H3_DE2_MUX0_VSU_BASE)) = 0;

    // Disable VI channel
    volatile H3_DE2_VI_TypeDef *vi = (H3_DE2_VI_TypeDef*)VI_BASE;
    vi->CFG[0].ATTR = 0;

    // Re-enable UI channel (channel 1)
    uint32_t fmt = H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
    H3_DE2_MUX0_UI->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN | fmt;
    H3_DE2_MUX0_UI->CFG[0].SIZE = screen_size;
    H3_DE2_MUX0_UI->CFG[0].COORD = 0;
    H3_DE2_MUX0_UI->CFG[0].PITCH = 4 * 1024;
    H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = fb_addr;
    H3_DE2_MUX0_UI->OVL_SIZE = screen_size;

    // Blender route back to UI
    H3_DE2_MUX0_BLD->ROUTE = 1;

    H3_DE2_MUX0_GLB->DBUFFER = 1;

    g_emu_mode = 0;
}

int de2_is_emu_mode(void) { return g_emu_mode; }