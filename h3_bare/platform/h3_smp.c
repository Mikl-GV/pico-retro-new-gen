// h3_smp.c — запуск вторичных ядер H3 (CPU1).
//
// Современный u-boot H3 (PSCI) большинство плат переводит в NON-SECURE
// состояние перед запуском прошивки. В non-secure мире регистры
// CPUCFG/PRCM (secure-only) игнорируют наши записи — ядро не стартует.
// Правильный путь: PSCI_CPU_ON (SMC #0, fid=0x84000003) — монитор u-boot
// остаётся в SRAM A1 и сам выполняет power/reset последовательность.
//
// Прямая (без PSCI) последовательность из u-boot psci.c (stock secure):
//   CPUCFG.priv0 = entry; CPU_RST=0; gen_ctrl and dev dbg; clamp release
//   (PRCM); pwroff clear; CPU_RST=3. Оставлена для случая secure-загрузки.

#include <stdint.h>
#include "h3.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

#define SUNXI_CPUCFG_BASE  0x01C19000u
#define SUNXI_PRCM_BASE    0x01F01400u

#define CFG_PRIV0          0x1A4u   /* адрес входа CPU1..CPU3 */
#define CFG_GEN_CTRL       0x184u
#define CFG_DBG_CTRL1      0x1E4u
#define CFG_CPU_RST(cpu)   (0x40u + (cpu) * 0x40u)

#define PRCM_CPU_PWROFF    0x100u
#define PRCM_CLAMP(cpu)    (0x140u + (cpu) * 0x4u)

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

static int h3_cpu_start_direct(int cpu, uint32_t entry) {
    uint32_t volatile* p;

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_PRIV0);
    *p = entry;
    __asm volatile("dsb" ::: "memory");

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_CPU_RST(cpu));
    *p = 0u;
    __asm volatile("dsb" ::: "memory");

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_GEN_CTRL);
    *p &= ~(1u << cpu);

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_DBG_CTRL1);
    *p &= ~(1u << cpu);

    /* питание: плавно снимаем power-clamp, затем clear power-gate */
    {
        uint32_t volatile* clamp = (uint32_t volatile*)(SUNXI_PRCM_BASE + PRCM_CLAMP(cpu));
        uint32_t tmp = 0x1FF;
        do { tmp >>= 1; *clamp = tmp; __asm volatile("dsb" ::: "memory"); } while (tmp);
        udelay(10000);
        p = (uint32_t volatile*)(SUNXI_PRCM_BASE + PRCM_CPU_PWROFF);
        *p &= ~(1u << cpu);
    }

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_CPU_RST(cpu));
    *p = 3u;   /* RSTEN | RST */
    __asm volatile("dsb" ::: "memory");

    p = (uint32_t volatile*)(SUNXI_CPUCFG_BASE + CFG_DBG_CTRL1);
    *p |= (1u << cpu);
    return 0;
}

int h3_cpu_start(int cpu, void (*entry)(void)) {
    if (cpu < 1 || cpu > 3) return -1;

    uint32_t scr = h3_read_scr();
    uint32_t ep  = (uint32_t)entry;
    printf("smp: CPU%u sec=0x%X entry=0x%X\n", (unsigned)cpu, (unsigned)scr, (unsigned)ep);

    if (scr & 1u) {
        /* Non-secure: только PSCI монитор u-boot может поднять ядро */
        uint32_t r = h3_psci_cpu_on((uint32_t)cpu, ep);
        printf("smp: PSCI CPU_ON ret=0x%X\n", (unsigned)r);
        return r == 0 ? 0 : -1;
    }

    /* Secure: прямая последовательность, как в u-boot psci.c */
    printf("smp: direct CPUS boot\n");
    return h3_cpu_start_direct(cpu, ep);
}