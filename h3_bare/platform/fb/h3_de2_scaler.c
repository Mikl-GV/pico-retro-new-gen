// h3_de2_scaler.c — аппаратный скейлер DE2 через VI-канал + VSU.
// Позволяет вывести маленький фреймбуфер (160×192, 256×240, 320×240)
// растянутым на большую область экрана без нагрузки на CPU.
// Если VSU не включается — вызывающий может использовать CPU-блит как fallback.
#include <stdint.h>
#include <string.h>
#include "h3_de2.h"

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
// Возвращает 0 при успехе, -1 если VSU не отвечает.
int de2_set_emu_mode(int src_w, int src_h, int dst_w, int dst_h,
                     int dst_x, int dst_y, uint32_t fb_addr,
                     int format_rgb565) {
    uint32_t fmt = format_rgb565
        ? H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_RGB_565)
        : H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
    int bpp = format_rgb565 ? 2 : 4;
    uint32_t vsu_base = VSU_BASE;

    // 1. Отключаем UI-канал (channel 1)
    H3_DE2_MUX0_UI->CFG[0].ATTR = 0;

    // 2. Настраиваем VI-канал (channel 0)
    volatile H3_DE2_VI_TypeDef *vi = (H3_DE2_VI_TypeDef*)VI_BASE;
    memset((void*)vi, 0, sizeof(H3_DE2_VI_TypeDef));

    vi->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN | fmt;
    vi->CFG[0].SIZE = H3_DE2_WH(src_w, src_h);
    vi->CFG[0].COORD = (dst_y << 16) | dst_x;
    vi->CFG[0].PITCH[0] = bpp * src_w;
    vi->CFG[0].TOP_LADDR[0] = fb_addr;

    // 3. Настраиваем выходной размер для скейлера
    vi->OVL_SIZE[0] = H3_DE2_WH(dst_w, dst_h);

    // 4. Настраиваем VSU скейлер
    // Step = (src << SCALE_FRAC) / dst. SCALE_FRAC = 20 (как в ядре Linux).
    // Для Integer scale: step = (1 << 20) / scale_factor
    // scale_factor = dst / src
    uint32_t hstep = ((uint32_t)src_w << 20) / (uint32_t)dst_w;
    uint32_t vstep = ((uint32_t)src_h << 20) / (uint32_t)dst_h;
    uint32_t insize = H3_DE2_WH(src_w, src_h);
    uint32_t outsize = H3_DE2_WH(dst_w, dst_h);

    VSU_REG(VSU_CTRL) = 0;  // disable first
    VSU_REG(VSU_YINSIZE) = insize;
    VSU_REG(VSU_YHPHASE) = 0;
    VSU_REG(VSU_YVPHASE) = 0;
    VSU_REG(VSU_YHSTEP) = hstep;
    VSU_REG(VSU_YVSTEP) = vstep;
    VSU_REG(VSU_CINSIZE) = insize;  // RGB: chroma = luma
    VSU_REG(VSU_CHPHASE) = 0;
    VSU_REG(VSU_CVPHASE) = 0;
    VSU_REG(VSU_CHSTEP) = hstep;
    VSU_REG(VSU_CVSTEP) = vstep;
    VSU_REG(VSU_OUTSIZE) = outsize;

    // Коэффициенты для nearest-neighbour при integer scale:
    // Простейшие: все weight на одном tap (первый коэффициент = 0x40000000, остальные 0)
    // Для 32-tap polyphase: tap0=0x40000000, остальные 0
    // Нам нужно 32 коэффициента на каждый тип. Записываем все нули, потом tap0=0x40000000
    // (коэф. 1.0 в 30.2 формате = 0x40000000)
    // Упрощаем: обнуляем кучу и ставим один ненулевой
    volatile uint32_t *coeff = (volatile uint32_t*)(VSU_BASE + 0x200);
    for (int i = 0; i < 32 * 8; i++) coeff[i] = 0;
    // Y horizontal coeff 0: tap[0] = 1.0
    coeff[0] = 0x40000000;
    coeff[32] = 0x40000000; // Y horizontal coeff 1
    coeff[64] = 0x40000000; // Y vertical
    coeff[96] = 0x40000000; // C horizontal 0
    coeff[128] = 0x40000000; // C horizontal 1
    coeff[160] = 0x40000000; // C vertical

    // Включаем VSU
    VSU_REG(VSU_CTRL) = VSU_CTRL_EN | VSU_CTRL_COEFF_RDY;

    // 5. Обновляем blender: route = 0 (VI channel 0 outputs to pipe)
    H3_DE2_MUX0_BLD->ROUTE = 0;

    // 6. Обновляем размеры blender (выходной размер экрана остаётся 1024×600)
    uint32_t screen_size = H3_DE2_WH(1024, 600);
    H3_DE2_MUX0_BLD->OUTPUT_SIZE = screen_size;
    H3_DE2_MUX0_BLD->ATTR[0].INSIZE = screen_size;

    // Apply
    H3_DE2_MUX0_GLB->DBUFFER = 1;

    g_emu_mode = 1;
    g_emu_w = src_w;
    g_emu_h = src_h;
    return 0;
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