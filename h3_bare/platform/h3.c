/**
 * @file h3.c
 *
 */
/* Copyright (C) 2018-2019 by Arjan van Vught mailto:info@orangepi-dmx.nl
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

#include <stdint.h>

#include "h3.h"

// ---- Управление MMU для DMA-когерентности (Cortex-A7, short-descriptor) ----
// U-Boot оставляет MMU включённым с flat 1MB-section маппингом DRAM
// (write-back). OHCI пишет ED/TD/HCCA в DRAM через DMA, а D-cache держит
// stale-копии — toggle-биты затираются. Правильное решение: пометить
// 1MB-секцию libh3_coherent_region как Normal Non-cacheable (TEX=001, C=0,
// B=0), тогда DMA-структуры видны и HC, и CPU напрямую, а D-cache для
// остальной памяти остаётся включённым (эмуляторы не тормозят).

void mmu_mark_uncached(uint32_t addr) {
    uint32_t l1_base;
    uint32_t entry, new_entry;
    uint32_t idx;

    __asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(l1_base));   // TTBR0
    l1_base &= ~0x3FFFu;    // выровнять на 16KB (таблица может быть 16KB)

    idx = (addr >> 20) & 0xFFF;                 // индекс 1MB-секции
    entry = ((volatile uint32_t*)l1_base)[idx];

    // Сохраняем base + все управляющие биты, меняем только атрибуты памяти:
    //   TEX[18:16] = 001 (Normal), C[3]=0, B[2]=0  -> Non-cacheable
    // AP/domain/XN/NS как было у U-Boot.
    new_entry = (entry & ~((0x7u << 16) | (1u << 3) | (1u << 2)))
              | (0x1u << 16)                     // TEX=001
              | (entry & ((1u << 0) | (1u << 1)));  // сохранить valid/section
    ((volatile uint32_t*)l1_base)[idx] = new_entry;

    __asm volatile("dsb" ::: "memory");
    // Invalidate TLB (все) + DSB + ISB — чтобы новый атрибут применился
    __asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}
