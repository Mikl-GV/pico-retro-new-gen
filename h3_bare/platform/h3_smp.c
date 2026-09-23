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

/* «Почта» CPU1 в SRAM A1: ядро пишет magic в 0x20 первой инструкцией,
 * core0 ждёт его после release. SRAM вне кэшей — видимость мгновенная,
 * независимо от состояния MMU/кэшей обоих ядер. */
#define CPU1_MAIL_ADDR 0x00000020u
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
//   ldr sp, [pc, #4]   ; SP = 0x5FE01000
//   ldr r0, [pc, #4]   ; r0 = entry (cpu1_entry в DRAM)
//   bx  r0
// После release опрашиваем SRAM-почту (0x20: magic). CPU1 ставит magic
// первой инструкцией — SRAM вне кэшей, видимость гарантирована.
// Если ядро не ожило — повторяем hold/release (до 5 попыток).
static int h3_secondary_start(int cpu, uint32_t entry) {
    const uint32_t tramp[5] = {
        0xE59FD004u,
        0xE59F0004u,
        0xE12FFF10u,
        0x5FE01000u,
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
        volatile uint32_t* mail = (volatile uint32_t*)CPU1_MAIL_ADDR;
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
                printf("smp: CPU%u alive after attempt %d\n", (unsigned)cpu, attempt + 1);
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
    printf("smp: CPU%u sec=0x%X entry=0x%X\n", (unsigned)cpu, (unsigned)scr, (unsigned)ep);

    if (scr & 1u) {
        /* Non-secure: регистры R_CPUCFG закрыты — будим через PSCI */
        uint32_t r = h3_psci_cpu_on((uint32_t)cpu, ep);
        printf("smp: PSCI CPU_ON ret=0x%X\n", (unsigned)r);
        return r == 0 ? 1 : 0;
    }

    printf("smp: direct R_CPUCFG boot (sram tramp 0x0)\n");
    return h3_secondary_start(cpu, ep);
}