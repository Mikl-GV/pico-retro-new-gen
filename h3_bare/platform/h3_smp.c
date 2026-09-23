// h3_smp.c — запуск вторичных ядер H3 (CPU1..CPU3) на примере CPU1.
//
// Последовательность взята из u-boot (arch/arm/cpu/armv7/sunxi/psci.c,
// sunxi_cpu_set_power / clamp_release / psci_cpu_on) для sun8i (H3):
//   1) записать адрес входа в CPUCFG.priv0 (0x01C191A4)
//   2) CPU_RST = 0 (удержать в сбросе)
//   3) gen_ctrl &= ~BIT(cpu) — сброс L1 (invalidate)
//   4) dbg_ctrl1 &= ~BIT(cpu) — lock CPU
//   5) power: PRCM cpu_pwr_clamp — плавное снятие (0x1ff>>1 ... 0)
//      затем cpu_pwroff &= ~BIT(cpu)
//   6) CPU_RST = BIT(1)|BIT(0) (RSTEN + release)
//   7) dbg_ctrl1 |= BIT(cpu) — unlock
//
// Вторичное ядро стартует БЕЗ MMU и кэшей (аппаратно выключены после
// снятия ресета) — для цикла SPI-дисплея это как раз удобно: прямые
// регистровые доступы и чтение DRAM без когерентности кэшей.
#include <stdint.h>
#include "h3.h"

#define SUNXI_CPUCFG_BASE  0x01C19000u
#define SUNXI_PRCM_BASE    0x01F01400u

#define CFG_PRIV0          0x1A4u   /* адрес входа CPU1..CPU3 */
#define CFG_GEN_CTRL       0x184u
#define CFG_DBG_CTRL1      0x1E4u
#define CFG_CPU_RST(cpu)   (0x40u + (cpu) * 0x40u)

#define PRCM_CPU_PWROFF    0x100u
#define PRCM_CLAMP(cpu)    (0x140u + (cpu) * 0x4u)

static void reg_set_bits(uint32_t addr, uint32_t bits) {
    *(volatile uint32_t*)addr |= bits;
    __asm volatile("dsb" ::: "memory");
}

static void reg_clear_bits(uint32_t addr, uint32_t bits) {
    *(volatile uint32_t*)addr &= ~bits;
    __asm volatile("dsb" ::: "memory");
}

int h3_cpu_start(int cpu, void (*entry)(void)) {
    if (cpu < 1 || cpu > 3) return -1;

    /* 1. адрес входа вторичного ядра */
    *(volatile uint32_t*)(SUNXI_CPUCFG_BASE + CFG_PRIV0) = (uint32_t)entry;
    __asm volatile("dsb" ::: "memory");

    /* 2. сброс целевого ядра */
    *(volatile uint32_t*)(SUNXI_CPUCFG_BASE + CFG_CPU_RST(cpu)) = 0;
    __asm volatile("dsb" ::: "memory");

    /* 3. invalidate L1 (gen_ctrl) */
    reg_clear_bits(SUNXI_CPUCFG_BASE + CFG_GEN_CTRL, (1u << cpu));

    /* 4. lock (disable external debug) */
    reg_clear_bits(SUNXI_CPUCFG_BASE + CFG_DBG_CTRL1, (1u << cpu));

    /* 5. питание: плавно снимаем clamp, затем clear power-gate */
    {
        volatile uint32_t* clamp = (volatile uint32_t*)(SUNXI_PRCM_BASE + PRCM_CLAMP(cpu));
        uint32_t tmp = 0x1FF;
        do { tmp >>= 1; *clamp = tmp; __asm volatile("dsb" ::: "memory"); } while (tmp);
        udelay(10000);
        reg_clear_bits(SUNXI_PRCM_BASE + PRCM_CPU_PWROFF, (1u << cpu));
    }

    /* 6. снять reset (RSTEN | RST) */
    *(volatile uint32_t*)(SUNXI_CPUCFG_BASE + CFG_CPU_RST(cpu)) = 3u;
    __asm volatile("dsb" ::: "memory");

    /* 7. unlock */
    reg_set_bits(SUNXI_CPUCFG_BASE + CFG_DBG_CTRL1, (1u << cpu));

    return 0;
}