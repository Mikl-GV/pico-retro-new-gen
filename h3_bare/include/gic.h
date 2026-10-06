#ifndef GIC_H3_H
#define GIC_H3_H

#include <stdint.h>

// GIC-400 v2 на H3 (ядро Secure — регистры доступны напрямую).
#define GIC_DIST_BASE    0x01C81000u
#define GIC_CPU_BASE     0x01C82000u

// Distributor offsets (GIC-400 / ARM GIC v2).
#define GICD_CTLR        (*(volatile uint32_t*)(GIC_DIST_BASE + 0x000u))
#define GICD_TYPER       (*(volatile uint32_t*)(GIC_DIST_BASE + 0x004u))
#define GICD_ISENABLER0  (*(volatile uint32_t*)(GIC_DIST_BASE + 0x100u))
#define GICD_ICENABLER0  (*(volatile uint32_t*)(GIC_DIST_BASE + 0x180u))
#define GICD_ICPENDR0    (*(volatile uint32_t*)(GIC_DIST_BASE + 0x280u))
#define GICD_IPRIORITYR  ((volatile uint8_t*)(GIC_DIST_BASE + 0x400u))
#define GICD_ITARGETSR   ((volatile uint8_t*)(GIC_DIST_BASE + 0x800u))
#define GICD_ICFGR       ((volatile uint32_t*)(GIC_DIST_BASE + 0xC00u))
#define GICD_IGROUPR0    (*(volatile uint32_t*)(GIC_DIST_BASE + 0x080u))

// CPU Interface offsets.
#define GICC_CTLR        (*(volatile uint32_t*)(GIC_CPU_BASE + 0x000u))
#define GICC_PMR         (*(volatile uint32_t*)(GIC_CPU_BASE + 0x004u))
#define GICC_BPR         (*(volatile uint32_t*)(GIC_CPU_BASE + 0x008u))
#define GICC_IAR         (*(volatile uint32_t*)(GIC_CPU_BASE + 0x00Cu))
#define GICC_EOIR        (*(volatile uint32_t*)(GIC_CPU_BASE + 0x010u))
#define GICC_RPR         (*(volatile uint32_t*)(GIC_CPU_BASE + 0x014u))

// Маски процессоров в GICD_ITARGETSR (CPU0=0x1, CPU1=0x2, CPU2=0x4, CPU3=0x8).
#define GIC_CPU_MASK_CPU2  0x4u

// r764: тип ISR. Для DMA-аудио ISR НЕ читает CUR_SRC (конвейер шины может
// отдать мусор во время передачи) — только переключает логический индекс
// буфера по типу прерывания (HALF=lli[0] готов, PKG=lli[1] готов).
typedef void (*gic_isr_t)(uint32_t intid);

// r765: инициализация ОБЩЕГО Distributor (GICD_CTLR, disable/enable всех,
// target/приоритет аудио-SPI 114 → CPU2). ВЫЗЫВАЕТ ТОЛЬКО core0 ОДИН РАЗ
// до старта вторичных ядер (main.c). Вторичные ядра Distributor НЕ трогают.
void gic_dist_init(void);

// r765: включение CPU Interface — единственное, что делает ядро, принимающее
// IRQ (CPU2): PMR/BPR/CTLR. Вызывать в контексте CPU2 (audio_core).
void gic_cpu_enable(void);

// Зарегистрировать обработчик IRQ (intid = 32 + SPI). NULL — снять.
void gic_register_isr(uint32_t intid, gic_isr_t isr);

// Диспетчер: вызывается из _irq_entry (startup.S) с r0 = INTID. Читает IAR,
// зовёт ISR; EOI пишет ассемблер (минимизация лага).
void gic_dispatch(uint32_t intid);

#endif /* GIC_H3_H */