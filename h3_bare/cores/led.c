// led.c — светодиоды Orange Pi Lite (H3): PA15 (красный, alive), PL10 (зелёный, SD).
// HIGH-active: зажечь = DAT=1, погасить = DAT=0 (подтверждено на железе).
// PL10: R_PIO — включаем такт PRCM + снимаем софт-ресет, пишем CFG с барьерами.
#include <stdint.h>
#include "h3_hs_timer.h"

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

// PA15: 1 = горит (HIGH-active). ВАЖНО: PA_DAT (порт A) — единственный владелец
// CPU1 (TFT-ядро: тач PA21, RST PA2). Сюда НЕ писать с CPU0 — иначе RMW-гонка
// с тач-CS теряет изменения (см. tft_drv.c). Моргалка живёт на CPU1:
// led_heartbeat_cpu1() вызывается из tft_core_main каждые 30 мс.
void led_set(int on) {
    if (on) PA_DAT |= (1u << 15);
    else    PA_DAT &= ~(1u << 15);
    mb();
}

// Индикатор «проц жив» — вызывать с CPU1 (tft_core_main, раз в ~30 мс).
// Моргает PA15: 0.5 с горит / 0.5 с гаснет. Не зависит от того, что делает
// CPU0 (завис эмулятор или нет) — если CPU1 крутится, проц жив.
void led_heartbeat_cpu1(void) {
    static uint32_t hb_t0 = 0;   // локальный счёт на CPU1 (не общий с CPU0)
    static int      hb_on = 0;
    uint32_t now = h3_hs_timer_lo_us();   // общий регистр, читается с любого ядра
    if (now < hb_t0) hb_t0 = now;         // r127: защита от wrap 32-бит (CURNT_LO ~44 c)
    if (now - hb_t0 >= 500000) {
        hb_t0 = now;
        hb_on = !hb_on;
        led_set(hb_on);
    }
}

// PL10: 1 = горит (HIGH-active)
void led_sd_on(void)  { PL_DAT |= (1u << 10); mb(); }
void led_sd_off(void) { PL_DAT &= ~(1u << 10); mb(); }
void led_sd_toggle(void) { PL_DAT ^= (1u << 10); mb(); }