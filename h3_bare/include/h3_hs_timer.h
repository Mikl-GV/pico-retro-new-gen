/**
 * @file h3_hs_timer.h
 *
 */
/* Copyright (C) 2018-2020 by Arjan van Vught mailto:info@orangepi-dmx.nl
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

#ifndef H3_HS_TIMER_H_
#define H3_HS_TIMER_H_

#include "h3.h"

#ifdef __cplusplus
extern "C" {
#endif

// r127: реальная частота HSTMR — ~96.8 МГц (CLK_SRC=PLL, не OSC24M).
// Измерено на железе: led_heartbeat_cpu1 ждёт 500000 «мкс» и даёт период
// моргания 0.248 с → 500000 ед = 0.124 с → 4.03 МГц единиц/с × 24 = 96.8 МГц.
// Делим на 97 → настоящие микросекунды (1 ед. = 1.003 мкс).
#define HSTMR_MHZ 97u

// r128: HSTMR — 64-битный счётчик (CURNT_HI:CURNT_LO). Нельзя делить только
// CURNT_LO: он wraps каждые ~44 с, lo_us прыгала на 44 млн «мкс» — у
// heartbeat и delay_ms сбивался период, а при зависании останавливался
// весь CPU1 (дисплей замирал). Читаем оба слова и делим 64-битное
// значение → wrap наступит только через 2^32 мкс ≈ 71 минуту.
inline static uint32_t h3_hs_timer_lo_us() {
	union { uint64_t u64; struct { uint32_t lo, hi; } w; } c;
	for (;;) {
		c.w.hi = H3_HS_TIMER->CURNT_HI;
		c.w.lo = H3_HS_TIMER->CURNT_LO;
		uint32_t hi2 = H3_HS_TIMER->CURNT_HI;
		if (hi2 == c.w.hi) break;      // не поймали перенос HI→LO между чтениями
	}
	return (uint32_t)(~(c.u64) / HSTMR_MHZ);
}

inline static void h3_hs_timer_delay(uint32_t d) {
	// Задержки короткие (<= 100000 тиков ≈ 1 мс) — wrap 32-бит CURNT_LO
	// (~44 с) пересечь не успевают; оставляем по LO.
	const uint32_t t1 = H3_HS_TIMER->CURNT_LO;

	do {
	} while (t1 - H3_HS_TIMER->CURNT_LO < d);
}

#ifdef __cplusplus
}
#endif

#endif /* H3_HS_TIMER_H_ */
