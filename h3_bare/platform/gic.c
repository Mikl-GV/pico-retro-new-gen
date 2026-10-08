// gic.c — GIC-400 v2 (Cortex-A7, H3). Аудио I2S0 (SPI 13 → INTID 45) на CPU2.
// Ядро Secure (sec=0x0) — регистры distributor/CPUIF доступны напрямую.
// _irq_entry (startup.S) читает IAR, зовёт gic_dispatch(intid), пишет EOIR.
//
// !!! r780: СЛОЙ НЕАКТИВЕН С r777 (звуковой вывод снят). Эти функции
// НИГДЕ не вызываются; не включать прерывания и не «оживлять» путь без
// полного аудита GIC-групп/приоритетов (см. AUDIT r780, P0-3).
// Защитно: GICD_CTLR НЕ включает группу 1 (иначе INTID уходит в Secure/FIQ,
// а _irq_entry ждёт группу 0 — зависание). Если слой понадобится —
// сначала вернуть GRP1 вместе с корректной конфигурацией банков FIQ.
#include <stdint.h>
#include "gic.h"

extern int printf(const char* fmt, ...);

#define GIC_SPI_BASE   32u          // INTID = 32 + SPI
#define GIC_MAX_INTID  160u         // H3: 157 источников (0..156)
#define GIC_SPURIOUS   1023u

#define GICD_CTLR_ENABLE    (1u << 0)
#define GICD_CTLR_ENABLE_GRP1 (1u << 1)
#define GICC_CTLR_ENABLE    (1u << 0)

// Таблица ISR. INTID 0..159 → слот. Для аудио нужен только 45 (GIC_AUDIO_INTID).
// В .coherent (uncached): регистрирует core0 (i2s_init), читает CPU2 (ISR) —
// без кэш-когерентности запись core0 не была бы видна CPU2.
static gic_isr_t g_isr[GIC_MAX_INTID] __attribute__((section(".coherent"), aligned(64)));

void gic_register_isr(uint32_t intid, gic_isr_t isr) {
    if (intid < GIC_MAX_INTID)
        g_isr[intid] = isr;
}

// Геттер: текущий обработчик INTID (для временной подмены тестами и
// восстановления). NULL, если не зарегистрирован.
gic_isr_t gic_get_isr(uint32_t intid) {
    if (intid >= GIC_MAX_INTID) return 0;
    return g_isr[intid];
}

// Диспетчер: вызывается из _irq_entry (startup.S) с r0 = INTID.
// Сам вызов ISR; EOI уже сделал ассемблер (минимизация лага).
void gic_dispatch(uint32_t intid) {
    if (intid == GIC_SPURIOUS) return;
    if (intid < GIC_MAX_INTID) {
        gic_isr_t isr = g_isr[intid];
        if (isr) { isr(intid); return; }
    }
    // Неизвестное прерывание — молча гасим (уже заакed в EOIR ассемблером).
}

// r765: Инициализация ОБЩЕГО Distributor — ВЫЗЫВАЕТСЯ ТОЛЬКО core0 ОДИН РАЗ,
// до старта вторичных ядер (main.c). Вторичные ядра (CPU1/CPU2) НЕ трогают
// Distributor (общий ресурс) — только свой CPU Interface (gic_cpu_enable).
// Это исключает гонку/затирание настроек, когда core0 уже поднял I2S/DMA,
// а CPU2 массово переписывает GICD.
void gic_dist_init(void) {
    uint32_t i;

    // --- Distributor: выключить, сбросить конфиг ---
    GICD_CTLR = 0;
    __asm volatile("dsb" ::: "memory");

    // Disable всех (0..156) — ICENABLER0/1/2/3/4 (по 32 прерывания).
    for (i = 0; i < (GIC_MAX_INTID + 31u) / 32u; i++)
        *(volatile uint32_t*)(GIC_DIST_BASE + 0x180u + i * 4u) = 0xFFFFFFFFu;
    // Clear pending (ICPENDR0..4)
    for (i = 0; i < (GIC_MAX_INTID + 31u) / 32u; i++)
        *(volatile uint32_t*)(GIC_DIST_BASE + 0x280u + i * 4u) = 0xFFFFFFFFu;
    __asm volatile("dsb" ::: "memory");

    // Приоритеты: все 0xA0 (нормальные), не-маскируемые не нужны.
    for (i = 0; i < GIC_MAX_INTID; i++)
        GICD_IPRIORITYR[i] = 0xA0;

    // r772: аудио-I2S0 = SPI 13 → INTID 45 (по DTS sun8i-h3: i2s@1c22000
    // interrupts=<0x00 0x0d 0x04>). Назначаем на CPU2 (0x04).
    const uint32_t audio_intid = GIC_AUDIO_INTID;   // 45
    GICD_ITARGETSR[audio_intid] = GIC_CPU_MASK_CPU2;
    GICD_IPRIORITYR[audio_intid] = 0x80;

    // Включить distributor. r780: ТОЛЬКО группа 0 (IRQ). Группа 1 = Secure
    // (FIQ) — её включение без банков FIQ увело бы INTID в «слепую» зону.
    GICD_CTLR = GICD_CTLR_ENABLE;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    // Включить конкретно аудио-INTID (I2S0, 45) в distributor.
    // r770/r772: для INTID ≥32 регистр ISENABLERn = 0x100 + (id/32)*4, бит = id%32.
    // Раньше писали ISENABLER0 = (1 << (id&31)) — это INTID<32, DMA не включался.
    *(volatile uint32_t*)(GIC_DIST_BASE + 0x100u + (audio_intid / 32u) * 4u)
        = (1u << (audio_intid & 31u));
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

// r765: Включение CPU Interface — вызывает ТОЛЬКО ядро, принимающее IRQ (CPU2),
// в своём контексте (audio_core). Distributor уже настроен core0.
void gic_cpu_enable(void) {
    GICC_PMR = 0xFFu;          // приоритетный маска: всё
    GICC_BPR = 0;              // групповой приоритет по умолчанию
    __asm volatile("dsb" ::: "memory");
    GICC_CTLR = GICC_CTLR_ENABLE;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

// r772: хелперы DMA-эпохи удалены — аудио-INTID теперь единый макрос
// GIC_AUDIO_INTID (45, I2S0) в gic.h.