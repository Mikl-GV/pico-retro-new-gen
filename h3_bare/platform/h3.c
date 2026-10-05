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
    uint32_t ref, new_entry;
    uint32_t idx;

    __asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(l1_base));   // TTBR0
    l1_base &= ~0x3FFFu;    // выровнять на 16KB (таблица может быть 16KB)

    idx = (addr >> 20) & 0xFFF;                 // индекс 1MB-секции

    // r724 (КОРЕНЬ «тест через кольцо молчит / Lynx рваный»): раньше брали
    // существующую запись U-Boot для этой секции и меняли только атрибуты
    // (TEX/C/B), СОХРАНЯЯ биты базы [31:20]. Но запись U-Boot для секции,
    // куда после роста BSS переехал .coherent (0x4A400000), оказалась мусором
    // (0xFFF9FFF3 — база 0xFFF00000, а не 0x4A400000). В результате core0
    // мапил .coherent на ФИЗИЧЕСКИЙ 0xFFF00000, а CPU2 (без MMU) работал с
    // настоящим 0x4A400000 — ядра «не видели» друг друга: кольцо не доливалось,
    // эмуляторный звук молчал, Lynx рвался.
    // Фикс: строим дескриптор от АДРЕСА (база = addr), а управляющие биты
    // (AP/domain/XN/nG/NS) берём из заведомо ВАЛИДНОЙ секции — 0x40000000, куда
    // всегда загружен наш образ .text (она наверняка правильно настроена U-Boot).
    ref = ((volatile uint32_t*)l1_base)[0x400];   // дескриптор секции 0x40000000

    new_entry = (addr & 0xFFF00000u)             // база = физический адрес секции
              | (ref & 0x0008DDF0u)              // сохранить: NS(19), AP(15:14,11:10), nG(12), domain(8:5), XN(4)
              | (1u << 16)                       // TEX=001 (Normal Non-cacheable)
              | (0u << 3) | (0u << 2)            // C=0, B=0 (uncached)
              | 0b10u;                           // section (bits[1:0]=0b10)

    ((volatile uint32_t*)l1_base)[idx] = new_entry;

    __asm volatile("dsb" ::: "memory");
    // Invalidate TLB (все) + DSB + ISB — чтобы новый атрибут применился
    __asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}
