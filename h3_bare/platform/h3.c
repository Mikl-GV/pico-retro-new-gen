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

