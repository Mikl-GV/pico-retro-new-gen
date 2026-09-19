// led.c — светодиоды Orange Pi Lite (H3): PA15 (красный, alive), PL10 (зелёный, SD).
// HIGH-active: зажечь = DAT=1, погасить = DAT=0 (подтверждено на железе).
// PL10: R_PIO — включаем такт PRCM + снимаем софт-ресет, пишем CFG с барьерами.
#include <stdint.h>

#define PRCM_BASE  0x01F01400u
#define PRCM_GATE0 (*(volatile uint32_t*)(PRCM_BASE + 0x28u))  // bus clk gating reg0
#define PRCM_GATE1 (*(volatile uint32_t*)(PRCM_BASE + 0x2Cu))  // bus clk gating reg1 (bit0=R_PIO)
#define PRCM_RST1  (*(volatile uint32_t*)(PRCM_BASE + 0x38u))  // soft reset reg1

#define PA_BASE  0x01C20800u
#define PA_CFG1  (*(volatile uint32_t*)(PA_BASE + 0x04u))
#define PA_DAT   (*(volatile uint32_t*)(PA_BASE + 0x10u))

#define PL_BASE  0x01F02C00u
#define PL_CFG0  (*(volatile uint32_t*)(PL_BASE + 0x00u))
#define PL_CFG1  (*(volatile uint32_t*)(PL_BASE + 0x04u))
#define PL_DAT   (*(volatile uint32_t*)(PL_BASE + 0x10u))

extern int printf(const char* fmt, ...);

static inline void mb(void) {
    __asm volatile("dsb sy" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

void led_init(void) {
    // --- PA15: output, такт PIO уже от U-Boot, пишем напрямую ---
    PA_CFG1 = (PA_CFG1 & ~(0xFu << 28)) | (0x1u << 28);
    mb();
    PA_DAT &= ~(1u << 15);   // погашен (0)
    mb();

    // --- PL10: R_PIO. Включаем такт APB0 + снимаем софт-ресет R_PIO.
    // По ccu-sun8i-r (H3): APB0_GATE0 (0x28) bit0 = R_PIO,
    // APB0_RESET (0x38) bit0 = R_PIO. Только бит 0, не всё разом, чтобы
    // случайно не разбудить/ресетнуть лишние домены (uart/timer/rsb).
    PRCM_GATE0 |= (1u << 0);
    mb();
    PRCM_GATE1 |= (1u << 0);
    mb();
    PRCM_RST1 |= (1u << 0);          // снять софт-ресет R_PIO
    mb();

    // пробуем оба регистра конфигурации (несколько раз, с барьерами)
    for (int i = 0; i < 4; i++) {
        PL_CFG0 = (PL_CFG0 & ~(0xFu << 8)) | (0x1u << 8);
        PL_CFG1 = (PL_CFG1 & ~(0xFu << 8)) | (0x1u << 8);
        mb();
    }
    PL_DAT &= ~(1u << 10);   // PL10 погашен (0)
    mb();

    // PL5 (S_PL_EINT5) — управление внешним питанием для геймпада Sega (PCF8574).
    // НЕ трогаем: питание включает U-Boot (boot.scr). Если выставить неверно —
    // можно отключить питание геймпада. Полное управление — позже отдельно.

    printf("led: initialized\n");
}

// PA15: 1 = горит (HIGH-active)
void led_set(int on) {
    if (on) PA_DAT |= (1u << 15);
    else    PA_DAT &= ~(1u << 15);
    mb();
}

// PL10: 1 = горит (HIGH-active)
void led_sd_on(void)  { PL_DAT |= (1u << 10); mb(); }
void led_sd_off(void) { PL_DAT &= ~(1u << 10); mb(); }
void led_sd_toggle(void) { PL_DAT ^= (1u << 10); mb(); }