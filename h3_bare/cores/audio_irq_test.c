// audio_irq_test.c — НЕЗАВИСИМЫЙ тест кольца I2S→DMA→FIFO на полных
// прерываниях DMA (PKG). Ничего общего с эмуляторами/меню/SLT/CPU2-кольцом:
//   core0: генерит синус 440 Гц → заполняет оба полубуфера (_dma_buf, uncached)
//   DMA:   крутит LLI-кольцо (2×32КБ) → I2S0_TX_FIFO по DRQ
//   ISR:    по прерыванию PKG (завершение LLI) доливает следующую порцию
//           синуса в освободившийся полубуфер + сбрасывает pending
// Чистый тон, единственный путь: генератор → DMA-буфер → IRQ → FIFO → I2S.
// Запуск: audio_irq_test(seconds). Печатает каждые 0.5 c живые регистры.
#include <stdint.h>
#include "h3.h"
#include "h3_dma.h"
#include "gic.h"
#include "i2s.h"

extern int printf(const char* fmt, ...);
extern unsigned char _dma_buf_start[];

#define AIQ_BUF_SZ    (64u * 1024u)
#define AIQ_HALF      (AIQ_BUF_SZ / 2u)      // 32 КБ = 4096 пар
#define AIQ_HALF_PAIRS (AIQ_HALF / 8u)
#define AIQ_FREQ      440u
#define AIQ_RATE      48000u

#define DMA_BASE_     0x01C02000u
#define AIQ_IRQ_PEND  (*(volatile uint32_t*)(DMA_BASE_ + 0x10u))
#define AIQ_IRQ_EN    (*(volatile uint32_t*)(DMA_BASE_ + 0x00u))
#define AIQ_CH_EN     (*(volatile uint32_t*)(DMA_BASE_ + 0x100u))
#define AIQ_CH_LLI    (*(volatile uint32_t*)(DMA_BASE_ + 0x108u))
#define AIQ_CH_CURSRC (*(volatile uint32_t*)(DMA_BASE_ + 0x110u))
#define AIQ_PKG_NUM   (*(volatile uint32_t*)(DMA_BASE_ + 0x130u))
#define AIQ_DMA_STA   (*(volatile uint32_t*)(DMA_BASE_ + 0x30u))
#define AIQ_I2S_CTRL  (*(volatile uint32_t*)0x01C22000u)
#define AIQ_I2S_TXCNT (*(volatile uint32_t*)0x01C22028u)
#define AIQ_I2S_ISTA  (*(volatile uint32_t*)0x01C2200Cu)

// 256-точечная синусоида (16-бит, амплитуда ~12000 — как рабочий sin_tab).
static const int16_t aiq_sin[256] = {
    0,295,589,882,1174,1465,1754,2042,2327,2609,2889,3166,3439,3709,3974,4235,
    4492,4743,4989,5230,5466,5696,5919,6136,6346,6549,6745,6933,7114,7287,7452,
    7608,7756,7895,8024,8145,8256,8358,8450,8533,8606,8669,8723,8766,8800,8824,
    8838,8843,8838,8824,8800,8766,8723,8669,8606,8533,8450,8358,8256,8145,8024,
    7895,7756,7608,7452,7287,7114,6933,6745,6549,6346,6136,5919,5696,5466,5230,
    4989,4743,4492,4235,3974,3709,3439,3166,2889,2609,2327,2042,1754,1465,1174,
    882,589,295,0,-295,-589,-882,-1174,-1465,-1754,-2042,-2327,-2609,-2889,-3166,
    -3439,-3709,-3974,-4235,-4492,-4743,-4989,-5230,-5466,-5696,-5919,-6136,-6346,
    -6549,-6745,-6933,-7114,-7287,-7452,-7608,-7756,-7895,-8024,-8145,-8256,-8358,
    -8450,-8533,-8606,-8669,-8723,-8766,-8800,-8824,-8838,-8843,-8838,-8824,-8800,
    -8766,-8723,-8669,-8606,-8533,-8450,-8358,-8256,-8145,-8024,-7895,-7756,-7608,
    -7452,-7287,-7114,-6933,-6745,-6549,-6346,-6136,-5919,-5696,-5466,-5230,-4989,
    -4743,-4492,-4235,-3974,-3709,-3439,-3166,-2889,-2609,-2327,-2042,-1754,-1465,
    -1174,-882,-589,-295,0,295,589,882,1174,1465,1754,2042,2327,2609,2889,3166,
    3439,3709,3974,4235,4492,4743,4989,5230,5466,5696,5919,6136,6346,6549,6745,
    6933,7114,7287,7452,7608,7756,7895,8024,8145,8256,8358,8450,8533,8606,8669,
    8723,8766,8800,8824,8838,8843,8838,8824,8800,8766,8723,8669,8606,8533,8450,
    8358,8256,8145,8024,7895,7756,7608,7452,7287,7114,6933,6745,6549,6346,6136,
    5919,5696,5466,5230,4989,4743,4492,4235,3974,3709,3439,3166,2889,2609,2327,
    2042,1754,1465,1174,882,589,295,0
};

static uint32_t aiq_phase = 0;
static uint32_t aiq_pkgs  = 0;

// Долив одного полубуфера (4096 пар) синусом от текущей фазы.
static void aiq_fill_half(uint32_t which) {
    volatile uint32_t* buf = (volatile uint32_t*)((uint32_t)_dma_buf_start + which * AIQ_HALF);
    uint32_t step = (uint32_t)(((uint64_t)AIQ_FREQ << 16) / AIQ_RATE);
    for (uint32_t i = 0; i < AIQ_HALF_PAIRS; i++) {
        int32_t s = aiq_sin[(aiq_phase >> 8) & 0xFF];
        buf[2 * i]     = (uint32_t)(uint16_t)s << 16;   // L: старшие 16 бит
        buf[2 * i + 1] = (uint32_t)(uint16_t)s << 16;   // R
        aiq_phase += step;
    }
    __asm volatile("dmb sy" ::: "memory");
}

// ISR: прерывание DMA PKG. Сброс pending + долив освободившегося полубуфера.
// Вызывается на CPU2 (GIC target). НЕ читает CUR_SRC (конвейер — мусор).
static void aiq_isr(uint32_t intid) {
    (void)intid;
    uint32_t n = h3_dma_audio_pkg_isr();   // сброс pending + счётчик
    aiq_fill_half(n & 1u);                 // чётный PKG → lli[0], нечётный → lli[1]
    aiq_pkgs = n;
}

// Сохранённая штатная ISR (на время теста подменяем, потом возвращаем).
static gic_isr_t aiq_old_isr = 0;

// Главная: независимый тест. seconds — длительность. 0 = бесконечно (ESC не
// слушаем — это железный тест, длительность фиксирована).
int audio_irq_test(int seconds) {
    if (seconds <= 0) seconds = 5;
    if (seconds > 60) seconds = 60;

    // CPU2-долив ставим на паузу — чтобы он не писал в буфер параллельно
    // (тест владеет буфером единолично).
    if (i2s_audio_core_active()) i2s_audio_cmd(AUDIO_CMD_PAUSE);

    // Подменяем ISR: своя, доливает синус по PKG. Сохраняем старую.
    aiq_old_isr = gic_get_isr(h3_dma_audio_intid());
    gic_register_isr(h3_dma_audio_intid(), aiq_isr);

    // Заполняем оба полубуфера с нуля.
    aiq_phase = 0;
    aiq_fill_half(0);
    aiq_fill_half(1);
    h3_dma_audio_pkg_reset();      // счётчик пакетов в ноль (D1)
    aiq_pkgs = 0;

    // Включаем DMA (LLI-кольцо уже настроено h3_dma_audio_init на _dma_buf).
    // Форсируем LLI на начало + EN, сброс pending.
    AIQ_IRQ_PEND = 0xFFFFFFFFu;
    __asm volatile("dsb" ::: "memory");
    AIQ_CH_LLI = (uint32_t)(uintptr_t)_dma_buf_start;   // заглушка-право: реальный LLI в h3_dma
    h3_dma_audio_start();

    printf("AIQ: start %d s, buf=0x%08X, I2S_CTRL=0x%08X, DMA_EN=0x%08X\n",
           seconds, (unsigned)(uintptr_t)_dma_buf_start,
           (unsigned)AIQ_I2S_CTRL, (unsigned)AIQ_CH_EN);

    for (int t = 0; t < seconds * 2; t++) {
        extern void udelay(unsigned long);
        udelay(500000);   // 0.5 c
        printf("AIQ: t=%.1f TXCNT=%lu PKG=%lu CURSRC=0x%08X IRQPEND=%08X ISTA=%08X STA=%08X\n",
               (double)(t + 1) * 0.5,
               (unsigned long)AIQ_I2S_TXCNT, (unsigned long)AIQ_PKG_NUM,
               (unsigned)AIQ_CH_CURSRC, (unsigned)AIQ_IRQ_PEND,
               (unsigned)AIQ_I2S_ISTA, (unsigned)AIQ_DMA_STA);
    }

    // Возвращаем как было: стоп DMA, восстановить штатную ISR и CPU2.
    h3_dma_audio_stop();
    AIQ_IRQ_PEND = 0xFFFFFFFFu;
    gic_register_isr(h3_dma_audio_intid(), aiq_old_isr);
    if (i2s_audio_core_active()) i2s_audio_cmd(AUDIO_CMD_RESUME);
    return 0;
}