// h3_smp.c — запуск вторичных ядер H3 (CPU1).
//
// РЕАЛЬНАЯ регистровая карта H3 (проверено bare-metal фреймворком
// allwinner-bare-metal для Orange Pi H3, uli):
//   R_CPUCFG (0x01F01C00):
//     RST_CTRL(cpu)  = +0x40 + cpu*0x40   (CPU1: 0x01F01C40)
//     CTRL(cpu)      = +0x44 + cpu*0x40
//     STATUS(cpu)    = +0x48 + cpu*0x40
//   Трамплин кладётся в SRAM A1 (0x00000000) — после сброса ядро
//   стартует именно оттуда. Запуск = RST_CTRL: 0 (удержать) -> 3.
//
// НЕ используется устаревшая карта A31 (CPUCFG 0x01C19000 / PRCM
// 0x01F01400) — на H3 эти записи не влияют на загрузку ядра.
//
// В non-secure мире (PSCI u-boot) регистры R_CPUCFG закрыты, поэтому там
// запуск через PSCI_CPU_ON (SMC #0, fid=0x84000003).

#include <stdint.h>
#include "h3.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

#define SUNXI_SRAM_A1_BASE 0x00000000u
#define R_CPUCFG_BASE      0x01F01C00u
#define CPU_RST_CTRL(cpu)  (R_CPUCFG_BASE + 0x40u + (cpu) * 0x40u)

/* «Почта» в SRAM A1: ядро пишет magic первой инструкцией, core0 ждёт его
 * после release. SRAM вне кэшей — видимость мгновенная, независимо от
 * состояния MMU/кэшей обоих ядер.
 * r1727: у каждого вторичного ядра СВОЯ ячейка magic. Раньше оба писали в
 * 0x20, а h3_secondary_start обнуляет её перед каждой попыткой — живой CPU2
 * из прошлой попытки успевал снова туда прописать magic, и core0 принимал
 * не то ядро за поднятое. CPU1/CPU2 обязаны писать каждый в свою. */
#define CPU1_MAIL_ADDR 0x000000C4u   /* CPU1 → core0 (startup.S cpu1_entry) */
#define CPU2_MAIL_ADDR 0x00000020u   /* CPU2 → core0 (startup.S cpu2_entry) */
#define CPU1_MAGIC     0x13579BDFu

/* PSCI 0.2 SMC32: CPU_ON */
#define PSCI_CPU_ON        0x84000003u

static uint32_t h3_read_scr(void) {
    uint32_t v;
    __asm volatile("mrc p15, 0, %0, c1, c1, 0" : "=r"(v));
    return v;
}

static uint32_t h3_psci_cpu_on(uint32_t cpu, uint32_t entry) {
    uint32_t ret;
    __asm volatile(
        "mov r0, %[fid]\n\t"
        "mov r1, %[cpu]\n\t"
        "mov r2, %[entry]\n\t"
        "mov r3, #0\n\t"
        "smc #0\n\t"
        "mov %[ret], r0\n\t"
        : [ret] "=r"(ret)
        : [fid] "r"((uint32_t)PSCI_CPU_ON), [cpu] "r"(cpu), [entry] "r"(entry)
        : "r0", "r1", "r2", "r3", "memory");
    return ret;
}

// Трамплин в SRAM A1 (0x00000000). Ядро после сброса стартует отсюда:
//   ldr sp, [pc, #4]   ; SP = stack_top (свой для каждого вторичного ядра)
//   ldr r0, [pc, #4]   ; r0 = entry (cpuN_entry в DRAM)
//   bx  r0
// После release опрашиваем SRAM-почту (0x20: magic). CPU ставит magic
// первой инструкцией — SRAM вне кэшей, видимость гарантирована.
// Если ядро не ожило — повторяем hold/release (до 5 попыток).
//
// r1347 (Ш3.3): SP раньше был жёстко 0x5FE01000 — SVC-стек CPU1 — для ОБОИХ
// ядер. CPU2 после bx cpu2_entry сразу переставлял SP на 0x5FC01000, но до
// первой инструкции C-пролога исключение (UND/ABT) ушло бы в обработчик
// startup.S с чужим банком SP и затерло бы стек CPU1. Теперь у каждого ядра
// свой стек, совпадающий с тем, что ставит его собственный пролог.
#define CPU1_SVC_STACK 0x5FE01000u
#define CPU2_SVC_STACK 0x5FC01000u

static int h3_secondary_start(int cpu, uint32_t entry) {
    const uint32_t stack_top = (cpu == 2) ? CPU2_SVC_STACK : CPU1_SVC_STACK;
    const uint32_t tramp[5] = {
        0xE59FD004u,
        0xE59F0004u,
        0xE12FFF10u,
        stack_top,
        entry,
    };
    volatile uint32_t* s = (volatile uint32_t*)SUNXI_SRAM_A1_BASE;
    for (int i = 0; i < 5; i++) s[i] = tramp[i];
    {
        uint32_t a = SUNXI_SRAM_A1_BASE & ~0x1Fu;
        uint32_t e = a + 64;
        for (; a < e; a += 32)
            __asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a));
    }
    __asm volatile("dsb" ::: "memory");

    for (int attempt = 0; attempt < 5; attempt++) {
        volatile uint32_t* mail = (volatile uint32_t*)((cpu == 2) ? CPU2_MAIL_ADDR : CPU1_MAIL_ADDR);
        *mail = 0;
        __asm volatile("dsb" ::: "memory");

        *(volatile uint32_t*)CPU_RST_CTRL(cpu) = 0u;
        __asm volatile("dsb" ::: "memory");
        udelay(2000);
        *(volatile uint32_t*)CPU_RST_CTRL(cpu) = 3u;
        __asm volatile("dsb" ::: "memory");

        for (uint32_t t = 0; t < 200; t++) {
            udelay(100);
            if (*mail == CPU1_MAGIC) {
                return 1;
            }
        }
        printf("smp: CPU%u not alive (attempt %d)\n", (unsigned)cpu, attempt + 1);
    }
    return 0;
}

int h3_cpu_start(int cpu, void (*entry)(void)) {
    if (cpu < 1 || cpu > 3) return -1;

    uint32_t scr = h3_read_scr();
    uint32_t ep  = (uint32_t)entry;

    if (scr & 1u) {
        /* Non-secure: регистры R_CPUCFG закрыты — будим через PSCI.
         * r1157 (E12): до 3 попыток. Одиночный CPU_ON при занятом/стартующем
         * ядре возвращал ошибку, и прошивка оставалась без аудио-ядра (звук
         * молчал). ALREADY_ON/ON_PENDING считаем успехом — ядро уже работает. */
        for (int attempt = 1; attempt <= 3; attempt++) {
            uint32_t r = h3_psci_cpu_on((uint32_t)cpu, ep);
            (void)r;
            if (r == 0) return 1;                    /* SUCCESS */
            if (r == 0xFFFFFFFCu) return 1;          /* ALREADY_ON */
            if (r == 0xFFFFFFFBu) return 1;          /* ON_PENDING */
            if (attempt < 3) udelay(2000);
        }
        printf("smp: PSCI CPU%u failed after 3 attempts\n", (unsigned)cpu);
        return 0;
    }

    int ok = h3_secondary_start(cpu, ep);
    /* r749: статус аудио-ядра — своя ячейка 0x90 (SRAM A1, uncached),
     * пишет cpu2_entry (startup.S, значение 0xA2). Не путать со статусом
     * CPU1 (0x24, TFT_STAT) — они разведены. */
    return ok;
}