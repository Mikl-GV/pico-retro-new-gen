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

// ---- Управление MMU (Cortex-A7, LPAE / long descriptors) ----
// r1202 (КОРЕНЬ падений Amiga): U-Boot 2025.01 на этом стенде работает в
// LPAE — TTBCR.EAE=1 (стенд: TTBCR=0x80000F00; F2=0x06 в LPAE-кодировке
// DFSR = Translation fault, level 2). Таблицы 64-битные: TTBR0 -> L0,
// записи L0 -> L1 (2-МБ блоки). Прежние функции писали 32-битные
// SHORT-дескрипторы в LPAE-таблицу — чужой формат (это же валило загрузку
// при TTBCR=0: r1197). Вторая беда: L1-таблицы U-Boot лежат в
// 0x5FFF1000/0x5FFF2000 — прямо под нашим стеком (0x60000000); глубокие
// вызовы (PUAE, FAT) затирали их, отсюда случайные Data Abort по разным
// адресам (0x4A800000, 0x4C000000, 0x467AB5F0). startup.S копирует
// L0+L1 в 0x4A40C000..0x4A420000 (вне стека) — карта остаётся целой.
//
// mmu_mark_uncached — no-op с r1206 (ниже история и основания). Атрибуты
// U-Boot не меняем: MAIR0=0xFFEEAA00 (byte0=Device, byte1=WT, byte2/3=WBWA),
// периферия = AttrIndx 0, DRAM = 2 (WB). DMA-структуры OHCI (ED/TD/HCCA)
// когерентны через cache_clean/invalidate, кольцо звука и межъядерные флаги
// живут в SRAM A1 (0x90/0x94, кольцо 0x1000/0x3000).

void mmu_mark_uncached(uint32_t addr)
{
    (void)addr;
    // r1206: функция — НАМЕРЕННЫЙ no-op. История и основания:
    // 1) U-Boot на этом стенде работает в LPAE (TTBCR.EAE=1, 64-битные
    //    таблицы). Прежняя реализация писала в них 32-битные short-секции —
    //    чужой формат, поэтому пометка uncached ФАКТИЧЕСКИ НЕ ПРИМЕНЯЛАСЬ
    //    (система всё это время жила с .coherent = write-back, и всё
    //    работало: USB, звук, игры).
    // 2) Попытки сделать честный NC (AttrIndx 4 с правкой MAIR и AttrIndx 7
    //    из готового MAIR-байта 0x44) ломали USB-энумерацию: `cc setup=15`
    //    -> Data Abort на addr=3 (r1203/r1205). WB-вариант (r1204) — работает.
    // 3) Когерентность обеспечивается без NC-маппинга:
    //    - USB: cache_clean/cache_invalidate вокруг HCCA/ED/TD и буферов;
    //    - звук: кольцо и индексы в SRAM A1 (r957/r929), CPU2 без MMU;
    //    - межъядерные флаги дублируются в SRAM (0x90/0x94).
    // Если когда-нибудь понадобится NC — сначала разобраться, почему он
    // рушит OHCI на этом ядре, и только потом менять карту.
}

// r1202: mmu_map_dram_cacheable УДАЛЕНА. Она прошивала short-дескрипторы в
// LPAE-таблицу (чужой формат) и лечила симптом, а не причину: sbrk-зона
// «фолтила» потому, что L1-таблица U-Boot в 0x5FFF1000 затиралась стеком.
// Теперь startup.S копирует L0+L1 в 0x4A40C000.. (см. выше), карта полная и
// целая — дорисовывать DRAM на старте не нужно.

// r1245-ДИАГ: контрольная сумма копий MMU-таблиц (L0 4 КБ + 4×L1 16 КБ =
// 20 КБ в 0x4A40C000..0x4A420000). Нужна, чтобы отличить порчу карты от
// ошибки ядра: Data Abort с DFSR=0x21 (External abort on translation table
// walk) означает, что walker не смог прочитать саму таблицу. Сверяем CRC
// до и после кадра эмулятора — изменение покажет виновника.
// Бит AF (10) в младших словах дескрипторов маскируется: для LPAE-block
// walker вправе выставить его сам, это не порча.
uint32_t mmu_tables_crc(void)
{
    const volatile uint32_t* p = (const volatile uint32_t*)0x4A40C000u;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < (0x5000u / 4u); i++) {
        uint32_t w = p[i];
        if ((i & 1u) == 0u)          // младшее слово 64-битной записи
            w &= ~(1u << 10);        // AF не считаем
        crc ^= w;
        for (int b = 0; b < 32; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// r1245: guard-маркеры вплотную к копиям MMU-таблиц. Таблицы занимают
// 0x4A40C000..0x4A420000; обе прилегающие зоны свободны (0x4A402FC0..0x4A40C000
// и 0x4A420000..0x4A700000), поэтому маркеры ничего не затирают. Если ядро
// пишет мимо буфера рядом с таблицами — увидим число испорченных слов, даже
// когда сам CRC таблиц ещё не изменился.
#define MMU_TAB_LO   0x4A40C000u
#define MMU_TAB_HI   0x4A420000u
#define MMU_GUARD_N  32u
#define MMU_GUARD_PAT 0xA5C3E10Fu

void mmu_guard_arm(void)
{
    volatile uint32_t* lo = (volatile uint32_t*)(MMU_TAB_LO - MMU_GUARD_N * 4u);
    volatile uint32_t* hi = (volatile uint32_t*)MMU_TAB_HI;

    for (uint32_t i = 0; i < MMU_GUARD_N; i++) {
        lo[i] = MMU_GUARD_PAT;
        hi[i] = MMU_GUARD_PAT;
    }
}

int mmu_guard_broken(void)
{
    const volatile uint32_t* lo = (const volatile uint32_t*)(MMU_TAB_LO - MMU_GUARD_N * 4u);
    const volatile uint32_t* hi = (const volatile uint32_t*)MMU_TAB_HI;
    int n = 0;

    for (uint32_t i = 0; i < MMU_GUARD_N; i++) {
        if (lo[i] != MMU_GUARD_PAT) n++;
        if (hi[i] != MMU_GUARD_PAT) n++;
    }
    return n;
}

