/**
 * @file h3_de2.h
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

#ifndef INCLUDE_H3_DE2_H_
#define INCLUDE_H3_DE2_H_

#include <h3.h>

#define H3_DE2_MUX0_BASE		(H3_DE_BASE + 0x100000)
#define H3_DE2_MUX1_BASE		(H3_DE_BASE + 0x200000)

#define H3_DE2_MUX0_GLB_BASE	(H3_DE2_MUX0_BASE + 0x00000)
#define H3_DE2_MUX1_GLB_BASE	(H3_DE2_MUX1_BASE + 0x00000)
	#define H3_DE2_MUX_GLB_CTL_EN	(1U << 0)

#define H3_DE2_MUX0_BLD_BASE	(H3_DE2_MUX0_BASE + 0x01000)
#define H3_DE2_MUX1_BLD_BASE	(H3_DE2_MUX1_BASE + 0x01000)

#define H3_DE2_MUX0_CHAN_BASE	(H3_DE2_MUX0_BASE + 0x02000)
#define H3_DE2_MUX1_CHAN_BASE	(H3_DE2_MUX1_BASE + 0x02000)
	#define H3_DE2_MUX_CHAN_SZ		0x1000

#define H3_DE2_MUX0_DCSC_BASE	(H3_DE2_MUX0_BASE + 0xb0000)
#define H3_DE2_MUX1_DCSC_BASE	(H3_DE2_MUX1_BASE + 0xb0000)

#define H3_DE2_MUX0_VSU_BASE	(H3_DE2_MUX0_BASE + 0x20000)
#define H3_DE2_MUX1_VSU_BASE	(H3_DE2_MUX1_BASE + 0x20000)

#define H3_DE2_MUX1_GSU1_BASE	(H3_DE2_MUX1_BASE + 0x30000)
#define H3_DE2_MUX0_GSU1_BASE	(H3_DE2_MUX0_BASE + 0x30000)

#define H3_DE2_MUX0_GSU2_BASE	(H3_DE2_MUX0_BASE + 0x40000)
#define H3_DE2_MUX1_GSU2_BASE	(H3_DE2_MUX1_BASE + 0x40000)

#define H3_DE2_MUX0_GSU3_BASE	(H3_DE2_MUX0_BASE + 0x50000)
#define H3_DE2_MUX1_GSU3_BASE	(H3_DE2_MUX1_BASE + 0x50000)

#define H3_DE2_MUX0_FCE_BASE	(H3_DE2_MUX0_BASE + 0xa0000)
#define H3_DE2_MUX1_FCE_BASE	(H3_DE2_MUX1_BASE + 0xa0000)

#define H3_DE2_MUX0_BWS_BASE	(H3_DE2_MUX0_BASE + 0xa2000)
#define H3_DE2_MUX1_BWS_BASE	(H3_DE2_MUX1_BASE + 0xa2000)

#define H3_DE2_MUX0_LTI_BASE	(H3_DE2_MUX0_BASE + 0xa4000)
#define H3_DE2_MUX1_LTI_BASE	(H3_DE2_MUX1_BASE + 0xa4000)

#define H3_DE2_MUX0_PEAK_BASE	(H3_DE2_MUX0_BASE + 0xa6000)
#define H3_DE2_MUX1_PEAK_BASE	(H3_DE2_MUX1_BASE + 0xa6000)

#define H3_DE2_MUX0_ASE_BASE	(H3_DE2_MUX0_BASE + 0xa8000)
#define H3_DE2_MUX1_ASE_BASE	(H3_DE2_MUX1_BASE + 0xa8000)

#define H3_DE2_MUX0_FCC_BASE	(H3_DE2_MUX0_BASE + 0xaa000)
#define H3_DE2_MUX1_FCC_BASE	(H3_DE2_MUX1_BASE + 0xaa000)

typedef struct T_H3_DE2 {
	__IO uint32_t GATE;	///< 0x00
	__IO uint32_t BUS;	///< 0x04
	__IO uint32_t RST;	///< 0x08
	__IO uint32_t DIV;	///< 0x0C
	__IO uint32_t SEL;	///< 0x10
} H3_DE2_TypeDef;

typedef struct T_H3_DE2_GLB {
	__IO uint32_t CTL;		///< 0x00
	__IO uint32_t STATUS;
	__IO uint32_t DBUFFER;
	__IO uint32_t SIZE;
} H3_DE2_GLB_TypeDef;

typedef struct T_H3_DE2_BLD {
	__IO uint32_t FCOLOR_CTL;
	struct {
		__IO uint32_t FCOLOR;
		__IO uint32_t INSIZE;
		__IO uint32_t OFFSET;
		__IO uint32_t RES;
	} ATTR[4];
	__IO uint32_t RES1[15];
	__IO uint32_t ROUTE;
	__IO uint32_t PREMULTIPLY;
	__IO uint32_t BKCOLOR;
	__IO uint32_t OUTPUT_SIZE;
	__IO uint32_t BLD_MODE[4];
	__IO uint32_t RES2[4];
	__IO uint32_t CK_CTL;
	__IO uint32_t CK_CFG;
	__IO uint32_t RES3[2];
	__IO uint32_t CK_MAX[4];
	__IO uint32_t RES4[4];
	__IO uint32_t CK_MIN[4];
	__IO uint32_t RES5[3];
	__IO uint32_t OUT_CTL;
}H3_DE2_BLD_TypeDef;

typedef struct T_H3_DE2_VI {
	struct {
		__IO uint32_t ATTR;
		__IO uint32_t SIZE;
		__IO uint32_t COORD;
		__IO uint32_t PITCH[3];
		__IO uint32_t TOP_LADDR[3];
		__IO uint32_t BOT_LADDR[3];
	} CFG[4];
	__IO uint32_t FCOLOR[4];
	__IO uint32_t TOP_HADDR[3];
	__IO uint32_t BOT_HADDR[3];
	__IO uint32_t OVL_SIZE[2];
	__IO uint32_t HORI[2];
	__IO uint32_t VERT[2];
} H3_DE2_VI_TypeDef;

typedef struct T_H3_DE2_UI {
	struct {
		__IO uint32_t ATTR;
		__IO uint32_t SIZE;
		__IO uint32_t COORD;
		__IO uint32_t PITCH;
		__IO uint32_t TOP_LADDR;
		__IO uint32_t BOT_LADDR;
		__IO uint32_t FCOLOR;
		__IO uint32_t RES;
	} CFG[4];
	__IO uint32_t TOP_HADDR;
	__IO uint32_t BOT_HADDR;
	__IO uint32_t OVL_SIZE;
} H3_DE2_UI_TypeDef;

typedef struct T_H3_DE2_CSC {
	__IO uint32_t CTL;
	__I  uint32_t RES[3];
	__IO uint32_t COEF11;
	__IO uint32_t COEF12;
	__IO uint32_t COEF13;
	__IO uint32_t COEF14;
	__IO uint32_t COEF21;
	__IO uint32_t COEF22;
	__IO uint32_t COEF23;
	__IO uint32_t COEF24;
	__IO uint32_t COEF31;
	__IO uint32_t COEF32;
	__IO uint32_t COEF33;
	__IO uint32_t COEF34;
} H3_DE2_CSC_TypeDef;

#define H3_DE2					((H3_DE2_TypeDef *) H3_DE_BASE)

#define H3_DE2_MUX0_GLB			((H3_DE2_GLB_TypeDef *) H3_DE2_MUX0_GLB_BASE)
#define H3_DE2_MUX1_GLB			((H3_DE2_GLB_TypeDef *) H3_DE2_MUX1_GLB_BASE)

#define H3_DE2_MUX0_BLD			((H3_DE2_BLD_TypeDef *) H3_DE2_MUX0_BLD_BASE)
#define H3_DE2_MUX1_BLD			((H3_DE2_BLD_TypeDef *) H3_DE2_MUX1_BLD_BASE)

#define H3_DE2_MUX0_UI			((H3_DE2_UI_TypeDef *) (H3_DE2_MUX0_CHAN_BASE + (1 * H3_DE2_MUX_CHAN_SZ)))
#define H3_DE2_MUX1_UI			((H3_DE2_UI_TypeDef *) (H3_DE2_MUX1_CHAN_BASE + (1 * H3_DE2_MUX_CHAN_SZ)))

#define H3_DE2_MUX0_CSC			((H3_DE2_CSC_TypeDef *) H3_DE2_MUX0_DCSC_BASE)
#define H3_DE2_MUX1_CSC			((H3_DE2_CSC_TypeDef *) H3_DE2_MUX1_DCSC_BASE)

#define H3_DE2_WH(w, h)				(((h - 1) << 16) | (w - 1))

/* r1489: аппаратный UI-скейлер DE2 — это GSU, ПО ЯДРУ LINUX
 * (drivers/gpu/drm/sun4i/sun8i_ui_scaler.c/.h, sun8i_mixer.c).
 *
 * Для H3 (sun8i_h3_mixer0_cfg: DE2, vi_scaler_num=1, ui_num=3, scaler_mask=0xf)
 * база GSU для UI-канала СЧИТАЕТСЯ по формуле sun8i_ui_scaler_base():
 *   base = DE2_VI_SCALER_UNIT_BASE + DE2_VI_SCALER_UNIT_SIZE * vi_scaler_num
 *        + DE2_UI_SCALER_UNIT_SIZE * (channel - vi_scaler_num)
 * DE2_VI_SCALER_UNIT_BASE=0x20000, UNIT_SIZE=0x20000, UI_UNIT_SIZE=0x10000.
 * Для UI-канала channel=1 (первый UI после 1 VI): 0x20000+0x20000+0 = 0x40000
 * относительно MUX0. ПРЕЖНЯЯ база 0x30000 (r1461) — НЕВЕРНА (это адрес
 * GSU1 второго микшера/чужая область), из-за неё r1466 и получался чёрный
 * экран: скейлер включался по несуществующему адресу.
 * Регистры — те же, что в SUN8I_SCALER_GSU_* (LI-model и bare-metal расходятся
 * только базой). */
#define H3_DE2_UI_SCALER_BASE		(H3_DE2_MUX0_BASE + 0x40000)
	#define H3_UI_SCAL_CTRL				0x000
	#define H3_UI_SCAL_OUTSIZE			0x040
	#define H3_UI_SCAL_INSIZE			0x080
	#define H3_UI_SCAL_HSTEP			0x088
	#define H3_UI_SCAL_VSTEP			0x08c
	#define H3_UI_SCAL_HPHASE			0x090
	#define H3_UI_SCAL_VPHASE			0x098
	#define H3_UI_SCAL_HCOEFF(i)		(0x200 + 4 * (i))
	#define H3_UI_SCAL_CTRL_EN			(1U << 0)
	#define H3_UI_SCAL_CTRL_COEFF_RDY	(1U << 4)
	#define H3_UI_SCAL_SCALE_FRAC		20
	#define H3_UI_SCAL_COEFF_COUNT		16
	#define H3_UI_SCAL_SIZE(w, h)		(((uint32_t)((h) - 1) << 16) | (uint32_t)((w) - 1))

int  h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h, uint32_t bpp,
			   uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh);
void h3_de2_ui_scaler_off(void);
void h3_de2_ui_restore(void);
void h3_de2_set_bkcolor(uint32_t argb);

#define H3_DE2_UI_CFG_ATTR_FMT(f)	((f & 0xf) << 8)
#define H3_DE2_UI_CFG_ATTR_EN		(1U << 0)
	#define H3_DE2_UI_FORMAT_XRGB_8888		4U
	#define H3_DE2_UI_FORMAT_RGB_565		10U

#endif /* INCLUDE_H3_DE2_H_ */
