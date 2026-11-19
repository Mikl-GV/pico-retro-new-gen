#pragma GCC push_options
#pragma GCC optimize ("Os")
/**
 * @file h3_de2.c
 *
 */
/* Copyright (C) 2020 by Arjan van Vught mailto:info@orangepi-dmx.nl
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * https://github.com/u-boot/u-boot/blob/master/drivers/video/sunxi/sunxi_de2.c
 *
 * Optimized for HDMI/DVI fixed resolution and now can be compiled with
 * -Wpedantic -Wsign-conversion
 *
 */
// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE2 display driver
 *
 * (C) Copyright 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "h3_de2.h"

#include "h3.h"
#include "h3_ccu.h"

#include "display_timing.h"
#include "h3_ui_scaler_coef.h"

/* r1461: размер выхода (режим HDMI), нужен UI-скейлеру как OUTSIZE. */
static uint32_t g_out_w = 0, g_out_h = 0;
/* r1462: база кадрового буфера меню (из de2_mode_set) — для восстановления слоя. */
static uint32_t g_fb_base = 0;

/*
 * Bits per pixel selector. Each value n is such that the bits-per-pixel is
 * 2 ^ n
 */
enum video_log2_bpp {
	VIDEO_BPP1	= 0,
	VIDEO_BPP2,
	VIDEO_BPP4,
	VIDEO_BPP8,
	VIDEO_BPP16,
	VIDEO_BPP32,
};

#define PLL_DE_ENABLE				(1U << 31)
#define PLL_DE_LOCK					(1U << 28)
#define PLL_DE_MODE_INTEGER			(1U << 24)
#define PLL_FACTOR_N_MASK			0x7F
#define PLL_FACTOR_N_SHIFT			8U
	#define PLL_DE_CTRL_N(n)			((((n) - 1) & PLL_FACTOR_N_MASK) << PLL_FACTOR_N_SHIFT)
#define PLL_FACTOR_M_MASK			0x0F
#define PLL_FACTOR_M_SHIFT			0U
	#define PLL_DE_CTRL_M(n)			((((n) - 1) & PLL_FACTOR_M_MASK) << PLL_FACTOR_M_SHIFT)

#define CCU_DE2_CLK_GATE			(1U << 31)
#define CCU_DE2_CLK_SRC_SEL_MASK	3U
#define CCU_DE2_CLK_SRC_SEL_SHIFT	24U
#define CCU_DE2_CLK_SRC_PLL_DE		1U
	#define PLL_DE_CTRL_CLK_SRC(n)	(n << CCU_DE2_CLK_SRC_SEL_SHIFT)

#define BIT(nr)                		(1UL << (nr))

static void clock_set_pll_de(const uint32_t clk) {
	const uint32_t m = 2; /* 12 MHz steps */

	H3_CCU->PLL_DE_CTRL = PLL_DE_ENABLE | PLL_DE_MODE_INTEGER | PLL_DE_CTRL_N(clk / (H3_F_24M / m))| PLL_DE_CTRL_M(m);

	uint32_t timeout = 10000000;
	while (!(H3_CCU->PLL_DE_CTRL & PLL_DE_LOCK)) {
		if (--timeout == 0) {
			extern int printf(const char*, ...);
			printf("WARN: PLL_DE not locked\n");
			break;
		}
	}
}

void de2_composer_init(void) {
	clock_set_pll_de(432000000);

	H3_CCU->BUS_CLK_GATING1 |= CCU_BUS_CLK_GATING1_DE;
	H3_CCU->BUS_SOFT_RESET1 |= CCU_BUS_SOFT_RESET1_DE;

	H3_CCU->DE_CLK = (H3_CCU->DE_CLK & ~(CCU_DE2_CLK_SRC_SEL_MASK << CCU_DE2_CLK_SRC_SEL_SHIFT)) | (PLL_DE_CTRL_CLK_SRC(CCU_DE2_CLK_SRC_PLL_DE));
	H3_CCU->DE_CLK |= (CCU_DE2_CLK_GATE);
}

void de2_mode_set(const struct display_timing *mode, const uint32_t bpp, uint32_t address) {
	const uint32_t size = H3_DE2_WH(mode->hactive.typ, mode->vactive.typ);
	uint32_t channel;
	uint32_t format;

	/* r1461: выход скейлера = размер режима */
	g_out_w = mode->hactive.typ;
	g_out_h = mode->vactive.typ;
	g_fb_base = address;

	H3_DE2->RST |= 1;
	H3_DE2->GATE |= BIT(0);
	H3_DE2->BUS |= BIT(0);
	H3_DE2->SEL &= ~(1U);

	H3_DE2_MUX0_GLB->CTL = H3_DE2_MUX_GLB_CTL_EN;
	H3_DE2_MUX0_GLB->STATUS = 0;
	H3_DE2_MUX0_GLB->SIZE = size;

	for (channel = 0; channel < 4; channel++) {
		void *ch = (void *)(H3_DE2_MUX0_CHAN_BASE + H3_DE2_MUX_CHAN_SZ * channel);
		memset(ch, 0, (channel == 0) ? sizeof(struct T_H3_DE2_VI) : sizeof(struct T_H3_DE2_UI));
	}

	memset(H3_DE2_MUX0_BLD, 0, sizeof(struct T_H3_DE2_BLD));

	H3_DE2_MUX0_BLD->FCOLOR_CTL = 0x00000101;
	H3_DE2_MUX0_BLD->ROUTE = 1;
	H3_DE2_MUX0_BLD->PREMULTIPLY = 0;
	H3_DE2_MUX0_BLD->BKCOLOR = 0xff000000;
	H3_DE2_MUX0_BLD->BLD_MODE[0] = 0x03010301;
	H3_DE2_MUX0_BLD->OUTPUT_SIZE = size;
	H3_DE2_MUX0_BLD->OUT_CTL = mode->flags & DISPLAY_FLAGS_INTERLACED ? 2 : 0;
	H3_DE2_MUX0_BLD->CK_CTL = 0;
	H3_DE2_MUX0_BLD->ATTR[0].FCOLOR = 0xff000000;
	H3_DE2_MUX0_BLD->ATTR[0].INSIZE = size;

	/* Disable all other units */
	*((volatile uint32_t *) H3_DE2_MUX0_VSU_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_GSU1_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_GSU2_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_GSU3_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_FCE_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_BWS_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_LTI_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_PEAK_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_ASE_BASE) = 0;
	*((volatile uint32_t *) H3_DE2_MUX0_FCC_BASE) = 0;

	H3_DE2_MUX0_CSC->CTL = 0;

	switch (bpp) {
	case 16:
		format = H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_RGB_565);
		break;
	case 32:
	default:
		format = H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
		break;
	}

	H3_DE2_MUX0_UI->CFG[0].ATTR = (H3_DE2_UI_CFG_ATTR_EN | format);
	H3_DE2_MUX0_UI->CFG[0].SIZE = size;
	H3_DE2_MUX0_UI->CFG[0].COORD = 0;
	H3_DE2_MUX0_UI->CFG[0].PITCH = (bpp / 8) * mode->hactive.typ;
	H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = address;
	H3_DE2_MUX0_UI->OVL_SIZE = size;

	H3_DE2_MUX0_GLB->DBUFFER = 1;
}

// r703 (C3): переключить адрес UI-канала DE2 (двойная буферизация direct-писателей
// BK/Vectrex). Вызов только из CONFIG_HDMI_DOUBLE_BUF (по умолчанию выкл.).
void h3_de2_set_ui_addr(uint32_t addr) {
	H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = addr;
	__asm volatile("dsb" ::: "memory");
}

extern int h3_hdmi_probe(void);
extern int h3_hdmi_enable(uint32_t panel_bpp, const struct display_timing *edid);
int __attribute__((cold)) h3_de2_init(struct display_timing *timing, uint32_t fbbase) {
	if (h3_hdmi_probe() != 0)
		return -1;

	de2_composer_init();
	de2_mode_set(timing, 1 << VIDEO_BPP32, fbbase);

	h3_hdmi_enable(1 << VIDEO_BPP32, timing);
	return 0;
}

static inline void h3_ui_scal_wr(uint32_t off, uint32_t v) {
	*(volatile uint32_t *)(H3_DE2_UI_SCALER_BASE + off) = v;
}

// r1489: слой UI показывает кадр ядра, DE растягивает его аппаратно (GSU).
// ПОРЯДОК ВЗЯТ ИЗ LINUX (sun8i_ui_scaler_setup + sun8i_ui_layer_atomic_update +
// sun8i_mixer_commit для DE2):
//   1) SRC/OUTSIZE, HSTEP/VSTEP (20 бит), HPHASE/VPHASE — при скейлере ВЫКЛ;
//   2) HCOEFF (16 фаз = lan2coefftab16 = наша таблица) — при скейлере ВЫКЛ;
//   3) затем единственная запись CTRL = EN | COEFF_RDY;
//   4) GLOBAL_DBUFFER = 1 — без неё регистры не защёлкиваются (чёрный экран).
// r1466 вёл себя иначе: скейлер включался ПОСЛЕ загрузки коэффициентов одной
// записью, а DBUFFER вообще не трогался, плюс база GSU была 0x30000 (неверно).
// bpp = 16 (RGB565) или 32 (XRGB8888). Возврат 0 — ок; -1 — не годятся размеры.
int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h, uint32_t bpp,
			  uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh) {
	uint32_t hstep, vstep, row, fmt;
	const uint32_t *coef;

	if (g_out_w == 0 || g_out_h == 0)
		return -1;
	if (src_w == 0 || src_h == 0 || dw == 0 || dh == 0)
		return -1;
	if (dx + dw > g_out_w || dy + dh > g_out_h)
		return -1;

	switch (bpp) {
	case 16:
		fmt = H3_DE2_UI_FORMAT_RGB_565;
		break;
	case 32:
		fmt = H3_DE2_UI_FORMAT_XRGB_8888;
		break;
	default:
		return -1;
	}

	/* Слой UI (как sun8i_ui_layer_update_coord/update_buffer):
	 * SIZE и OVL_SIZE = ИСХОДНЫЙ размер; COORD(UI) = 0 (позиция наложения —
	 * только в BLEND_ATTR_COORD); PITCH = ширина [байт]; TOP_LADDR = адрес. */
	H3_DE2_MUX0_UI->CFG[0].SIZE = H3_DE2_WH(src_w, src_h);
	H3_DE2_MUX0_UI->CFG[0].COORD = 0;
	H3_DE2_MUX0_UI->CFG[0].PITCH = (bpp / 8u) * src_w;
	H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = addr;
	H3_DE2_MUX0_UI->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN |
		H3_DE2_UI_CFG_ATTR_FMT(fmt);
	/* r1490: OVL_SIZE = ИСХОДНЫЙ размер кадра (окно выборки скейлера), как
	 * sun8i_ui_layer_update_coord: UI_OVL_SIZE = insize. Если здесь выходной
	 * размер (800×600 при источнике 640×480), строки читаются с «чужим»
	 * шагом VSTEP и разъезжаются. */
	H3_DE2_MUX0_UI->OVL_SIZE = H3_DE2_WH(src_w, src_h);
	/* BLD-атрибуты: ВЫХОДНОЙ прямоугольник, куда скейлер кладёт кадр
	 * (как sun8i_ui_layer_update_coord: BLEND_ATTR_INSIZE = outsize,
	 * BLEND_ATTR_COORD = dst). */
	H3_DE2_MUX0_BLD->ATTR[0].INSIZE = H3_DE2_WH(dw, dh);
	H3_DE2_MUX0_BLD->ATTR[0].OFFSET = (dy << 16) | dx;

	/* 1:1 — скейлер не нужен, но выключаем по-честному. */
	if (src_w == dw && src_h == dh) {
		h3_ui_scal_wr(H3_UI_SCAL_CTRL, 0);
		H3_DE2_MUX0_GLB->DBUFFER = 1;   // r1489: commit обязателен
		__asm volatile("dsb" ::: "memory");
		return 0;
	}

	hstep = (uint32_t)(((uint64_t)src_w << H3_UI_SCAL_SCALE_FRAC) / dw);
	vstep = (uint32_t)(((uint64_t)src_h << H3_UI_SCAL_SCALE_FRAC) / dh);

	/* 1) размеры и шаг — при CTRL=0 (скейлер выключен) */
	h3_ui_scal_wr(H3_UI_SCAL_CTRL, 0);
	__asm volatile("dsb" ::: "memory");
	h3_ui_scal_wr(H3_UI_SCAL_OUTSIZE, H3_UI_SCAL_SIZE(dw, dh));
	h3_ui_scal_wr(H3_UI_SCAL_INSIZE, H3_UI_SCAL_SIZE(src_w, src_h));
	h3_ui_scal_wr(H3_UI_SCAL_HSTEP, hstep);
	h3_ui_scal_wr(H3_UI_SCAL_VSTEP, vstep);
	h3_ui_scal_wr(H3_UI_SCAL_HPHASE, 0);
	h3_ui_scal_wr(H3_UI_SCAL_VPHASE, 0);

	/* 2) коэффициенты — индекс строки как sun8i_ui_scaler_coef_index(),
	 * таблица lan2coefftab16 = h3_ui_scaler_coef (16 фаз). */
	{
		unsigned int scale, int_part, float_part;
		scale = hstep >> (H3_UI_SCAL_SCALE_FRAC - 3);
		int_part = scale >> 3;
		float_part = scale & 0x7;
		switch (int_part) {
		case 0:  row = 0;  break;
		case 1:  row = float_part; break;
		case 2:  row = 8 + (float_part >> 1); break;
		case 3:  row = 12; break;
		case 4:  row = 13; break;
		default: row = 14; break;
		}
	}
	coef = &h3_ui_scaler_coef[row * H3_UI_SCAL_COEFF_COUNT];
	for (uint32_t i = 0; i < H3_UI_SCAL_COEFF_COUNT; i++)
		h3_ui_scal_wr(H3_UI_SCAL_HCOEFF(i), coef[i]);

	/* 3) включение: EN | COEFF_RDY одной записью (как sun8i_ui_scaler_enable). */
	__asm volatile("dsb" ::: "memory");
	h3_ui_scal_wr(H3_UI_SCAL_CTRL, H3_UI_SCAL_CTRL_EN | H3_UI_SCAL_CTRL_COEFF_RDY);
	/* 4) commit регистров — DE продолжит со старыми значениями без неё. */
	H3_DE2_MUX0_GLB->DBUFFER = 1;
	__asm volatile("dsb" ::: "memory");
	return 0;
}

// r1462: вернуть слой 1:1 на экранный буфер меню (адрес запомнен в de2_mode_set).
void h3_de2_ui_restore(void) {
	if (g_out_w == 0 || g_out_h == 0)
		return;
	h3_ui_scal_wr(H3_UI_SCAL_CTRL, 0);
	/* UI-слой: полный экран, адрес меню (как de2_mode_set). COORD(UI)=0 —
	 * позиция задаётся только в BLEND_ATTR_COORD. */
	H3_DE2_MUX0_UI->CFG[0].SIZE = H3_DE2_WH(g_out_w, g_out_h);
	H3_DE2_MUX0_UI->CFG[0].COORD = 0;
	H3_DE2_MUX0_UI->CFG[0].PITCH = g_out_w * 4u;
	H3_DE2_MUX0_UI->CFG[0].TOP_LADDR = g_fb_base;
	H3_DE2_MUX0_UI->CFG[0].ATTR = H3_DE2_UI_CFG_ATTR_EN |
		H3_DE2_UI_CFG_ATTR_FMT(H3_DE2_UI_FORMAT_XRGB_8888);
	H3_DE2_MUX0_UI->OVL_SIZE = H3_DE2_WH(g_out_w, g_out_h);
	/* r1490: BLD-атрибуты — обратно на весь экран (скейлер/слой pxt их
	 * менял на (800,600)/(112,0)). Иначе после выхода меню «обрезано»
	 * областью 800×600+сдвигом. */
	H3_DE2_MUX0_BLD->ATTR[0].INSIZE = H3_DE2_WH(g_out_w, g_out_h);
	H3_DE2_MUX0_BLD->ATTR[0].OFFSET = 0;
	/* r1489: commit — иначе смена размеров/адреса не защёлкнется (как в
	 * sun8i_mixer_commit: GLOBAL_DBUFFER=1 после настройки слоёв). */
	H3_DE2_MUX0_GLB->DBUFFER = 1;
	__asm volatile("dsb" ::: "memory");
}

// r1466: цвет бордюра (поле вокруг нативного кадра). Пишем и BKCOLOR микшера,
// и FCOLOR слоя 0 — иначе непокрытая область остаётся чёрной и «растр не виден».
void h3_de2_set_bkcolor(uint32_t argb) {
	H3_DE2_MUX0_BLD->BKCOLOR = argb;
	H3_DE2_MUX0_BLD->ATTR[0].FCOLOR = argb;
	__asm volatile("dsb" ::: "memory");
}

// r1461: выключить скейлер (без перестройки слоя).
void h3_de2_ui_scaler_off(void) {
	h3_ui_scal_wr(H3_UI_SCAL_CTRL, 0);
	__asm volatile("dsb" ::: "memory");
}

