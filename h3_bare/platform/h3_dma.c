// h3_dma.c — минимальный драйвер sunxi-DMA (H3, sun8i) для I2S0 TX.
//
// Задача (r740, решение владельца): единственный писатель TX FIFO — железо.
// CPU2-долив (audio_core.c) и ручной вывод в FIFO упразднены: DMA циклически
// читает uncached RAM-буфер (.dma_buf) и пишет в I2S0_TX_FIFO (0x01C22020) по
// аппаратному DRQ (I2S0_TX=3) — залипание CPU2 на записи в полный FIFO
// (AHB-stall, ST:2 EN=EX+1, ABT:0) исключено архитектурно.
//
// Референс: linux-драйвер drivers/dma/sun6i-dma.c (конфиг sun8i_h3_dma_cfg)
// + Allwinner_H3_Datasheet V1.2 раздел 4.11 (DMA) и таблица DRQ (4-1).
//
//   DMAC base       0x01C02000
//   DMA_STA         0x30      bitN = канал N busy
//   Канал N:        +0x100 + N*0x40
//     EN            0x00
//     PAU           0x04
//     LLI_ADDR      0x08  (адрес LLI-дескриптора, word-aligned)
//     CFG           0x0C  (RO — активная конфигурация, заполняется из LLI)
//     CUR_SRC       0x10  (RO — текущий адрес источника; по нему продюсер
//                          определяет, сколько DMA уже сыграл)
//     PARA          0x1C  (WAIT_CYC)
//
// DRQ (Table 4-1): SRC: 0=SRAM, 1=SDRAM; DST: 3=I2S/PCM 0_TX.
// Битовые поля H3 (sun8i_h3): SRC_BURST [7:6], SRC_WIDTH [10:9],
// DST_BURST [23:22], DST_WIDTH [26:25], SRC_DRQ [4:0], DST_DRQ [20:16],
// SRC_MODE [5], DST_MODE [21].
#include <stdint.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_dma.h"

extern int printf(const char* fmt, ...);

#define DMA_BASE            0x01C02000u
#define DMA_IRQ_EN          (*(volatile uint32_t*)(DMA_BASE + 0x00u))
#define DMA_IRQ_PEND        (*(volatile uint32_t*)(DMA_BASE + 0x10u))
#define DMA_AUTO_GATE       (*(volatile uint32_t*)(DMA_BASE + 0x28u))
#define DMA_STA             (*(volatile uint32_t*)(DMA_BASE + 0x30u))
#define DMA_PKG_NUM         (*(volatile uint32_t*)(DMA_BASE + 0x130u))

#define CH_EN(n)      (*(volatile uint32_t*)(DMA_BASE + 0x100u + (n)*0x40u + 0x00u))
#define CH_PAU(n)     (*(volatile uint32_t*)(DMA_BASE + 0x100u + (n)*0x40u + 0x04u))
#define CH_LLI(n)     (*(volatile uint32_t*)(DMA_BASE + 0x100u + (n)*0x40u + 0x08u))
#define CH_CUR_SRC(n) (*(volatile uint32_t*)(DMA_BASE + 0x100u + (n)*0x40u + 0x10u))

#define LLI_LAST_ITEM     0xfffff800u   // признак последнего дескриптора
#define NORMAL_WAIT       8u            // WAIT_CYC (как в sun6i-dma)
#define DRQ_SDRAM         1             // источник — память
#define DRQ_I2S0_TX       3             // приёмник — I2S/PCM 0 TX

// Битовые поля конфигурации канала (H3 / sun8i_h3).
#define CFG_SRC_WIDTH(x)  ((x) << 9)    // 0=8б 1=16б 2=32б 3=64б
#define CFG_SRC_BURST(x)  ((x) << 6)    // 0=1 1=4 2=8 3=16
#define CFG_SRC_DRQ(x)    ((x) << 0)
#define CFG_SRC_MODE(x)   ((x) << 5)    // 0=linear 1=IO
#define CFG_DST_WIDTH(x)  ((x) << 25)
#define CFG_DST_BURST(x)  ((x) << 22)
#define CFG_DST_DRQ(x)    ((x) << 16)
#define CFG_DST_MODE(x)   ((x) << 21)   // 0=linear 1=IO

// LLI-дескриптор: 6 слов (как struct sun6i_dma_lli в sun6i-dma.c).
struct dma_lli {
    uint32_t cfg;
    uint32_t src;   // физический адрес источника
    uint32_t dst;   // физический адрес приёмника
    uint32_t len;   // длина пакета в байтах (кратна burst*width)
    uint32_t para;
    uint32_t next;  // физ. адрес след. LLI; 0xfffff800 = последний
};

// LLI читает сам DMAC — обязан быть в uncached-памяти. Кладём в .coherent
// (секция помечена uncached через mmu_mark_uncached, main.c). Маленький
// (24 байта), до L1-таблицы (0x4A40C000) запас есть (ASSERT в linker.ld).
// Для циклической передачи — ДВА LLI (половинки буфера), last.next → первый:
// так DMA после каждого LLI переходит к следующему, после второго — снова
// к первому, и крутится вечно. (Один LLI со len=весь буфер сделал бы один
// проход и остановился на 0xfffff800.)
static struct dma_lli g_audio_lli[2] __attribute__((section(".coherent"), aligned(64)));

static int g_ch = -1;
static uint32_t g_buf_phys = 0;   // физический адрес аудио-буфера (uncached)
static uint32_t g_buf_bytes = 0;

int h3_dma_audio_init(int ch, uint32_t buf_phys, uint32_t buf_bytes) {
    // Тактирование DMAC: BUS_CLK_GATING0 bit6 + BUS_SOFT_RESET0 bit6 (CCU).
    H3_CCU->BUS_CLK_GATING0 |= CCU_BUS_CLK_GATING0_DMA;
    __asm volatile("dsb" ::: "memory");

    // r744: ПОЛНЫЙ сброс DMAC ПЕРЕД настройкой. Лог r743 показал: LLI/CUR_SRC
    // читались мусором (0x034C5053/0x4E4F4765), а TXCNT рос ~46к/с — DMAC не
    // был сброшен от U-Boot и продолжал передачу по СТАРОМУ/мусорному LLI
    // (наш LLI не принимался, звук не шёл). Поэтому: assert+deassert reset,
    // стоп канала, только потом конфигурация.
    // r759: на текущем U-Boot 2026.07 DMAC приходит чистым (все нули —
    // проверено дампом r758), сброс оставлен как страховка от мусора.
    //
    H3_CCU->BUS_SOFT_RESET0 &= ~CCU_BUS_SOFT_RESET0_DMA;
    __asm volatile("dsb" ::: "memory");
    udelay(10);
    H3_CCU->BUS_SOFT_RESET0 |= CCU_BUS_SOFT_RESET0_DMA;
    __asm volatile("dsb" ::: "memory");
    udelay(10);

    // Autogate (H3: DMA_AUTO_GATE_REG 0x28 = 0x4, см. sun6i-enable-h3).
    DMA_AUTO_GATE = 0x4u;
    __asm volatile("dsb" ::: "memory");

    // Стоп канала и сброс в ноль (после reset регистры уже нули, страховка).
    CH_EN(ch) = 0;
    CH_PAU(ch) = 0;
    __asm volatile("dsb" ::: "memory");

    // Конфигурация пакета: SRC = RAM (32-бит слова, burst 4) →
    // DST = I2S0_TX FIFO (32-бит, burst 1 по DRQ, IO-mode).
    // Ширина 32 бита = 2 (ilog2(4)); burst 4 = 1; burst 1 = 0.
    uint32_t cfg = CFG_SRC_WIDTH(2) | CFG_SRC_BURST(1) | CFG_SRC_DRQ(DRQ_SDRAM) | CFG_SRC_MODE(0)
                 | CFG_DST_WIDTH(2) | CFG_DST_BURST(0) | CFG_DST_DRQ(DRQ_I2S0_TX) | CFG_DST_MODE(1);

    // Два LLI по полбуфера (чётное число байт обязательно; 32 КБ кратны 16).
    uint32_t half = buf_bytes / 2;
    for (int i = 0; i < 2; i++) {
        struct dma_lli* l = &g_audio_lli[i];
        l->cfg  = cfg;
        l->src  = buf_phys + (uint32_t)i * half;
        l->dst  = 0x01C22020u;           // I2S_FIFO_TX
        l->len  = half;
        l->para = NORMAL_WAIT;
        l->next = (uint32_t)(uintptr_t)&g_audio_lli[(i + 1) & 1];  // цикл: 0→1→0
    }
    __asm volatile("dmb sy" ::: "memory");   // LLI-структура видна DMAC
    // flush D$: LLI лежит в .coherent, но после сброса заново чистим строки
    // (иначе DMAC через физические адреса может прочитать stale кэш).
    for (uint32_t a = (uint32_t)(uintptr_t)&g_audio_lli[0] & ~0x1Fu;
         a < (uint32_t)(uintptr_t)&g_audio_lli[2]; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));   // DCCIMVAC
    __asm volatile("dsb" ::: "memory");

g_ch = ch;
    g_buf_phys = buf_phys;
    g_buf_bytes = buf_bytes;

    // r764: включаем PKG-прерывание канала 0 (DMA0_PKG_IRQ_EN, bit1 в
    // IRQ_EN0). При каждом завершённом пакете (LLI-переходе) DMA дёрнет IRQ —
    // ISR сбросит pending и переключит логический буфер. Без поллинга.
    // DMA_IRQ_EN0: бит0=HALF, бит1=PKG, бит2=QUEUE (сдвиг 4*ch).
    DMA_IRQ_EN |= (1u << 1) << (ch * 4);   // PKG_EN ch0 → 0x02

    return 0;
}

// r764: INTID аудио-DMA (SPI 82 → 32+82 = 114). Для GIC.
uint32_t h3_dma_audio_intid(void) { return 32u + 82u; }

// r764: счётчик завершённых пакетов (PKG-прерываний). Пишет ISR (CPU2),
// НЕ читает CUR_SRC (конвейер шины может отдать мусор). Каждый PKG = один
// завершённый LLI (32768 байт = 4096 пар), lli чередуются 0/1/0/1.
// .coherent — чтобы core0-диагностика видела без кэш-проблем.
static volatile uint32_t g_pkg_cnt __attribute__((section(".coherent"), aligned(64))) = 0;

// Вызывается из ISR (CPU2) при PKG: инкремент счётчика и сброс pending.
// Возвращает: количество завершённых пакетов с последнего сброса.
uint32_t h3_dma_audio_pkg_isr(void) {
    DMA_IRQ_PEND = 0xFFFFFFFFu;   // write-1-clear всех бит
    __asm volatile("dsb" ::: "memory");
    return ++g_pkg_cnt;
}

// Сброс счётчика (при ring_reset/init). Вызывает core0.
void h3_dma_audio_pkg_reset(void) {
    g_pkg_cnt = 0;
    __asm volatile("dmb sy" ::: "memory");
}

void h3_dma_audio_start(void) {
    if (g_ch < 0) return;
    CH_LLI(g_ch) = (uint32_t)(uintptr_t)&g_audio_lli[0];
    __asm volatile("dsb" ::: "memory");
    // r762: сброс DMA_IRQ_PEND (write-1-clear) перед стартом. H3 DMA в
    // cyclic-режиме после каждого пакета взводит PKG/HALF в IRQ_PEND; мы IRQ
    // не используем и никогда не сбрасывали — pending копился, и после
    // ~34 пакетов канал замирал (симптом «PKG=34, SRC застыл, underrun»).
    // Linux в ISR делает writel(status, IRQ_STAT); здесь — то же перед EN.
    DMA_IRQ_PEND = 0xFFFFFFFFu;   // write-1-clear всех бит
    __asm volatile("dsb" ::: "memory");
    CH_EN(g_ch) = 1;
    __asm volatile("dsb" ::: "memory");
}

void h3_dma_audio_stop(void) {
    if (g_ch < 0) return;
    CH_EN(g_ch) = 0;
    __asm volatile("dsb" ::: "memory");
}

// Текущий адрес, который DMA сейчас читает. Продюсер считает потребление:
//   consumed_bytes = cur - buf_phys   (в пределах [0, buf_bytes))
uint32_t h3_dma_audio_cur_pos(void) {
    if (g_ch < 0) return 0;
    return CH_CUR_SRC(g_ch);
}