// led.c — светодиоды Orange Pi Lite (H3): PA15 (красный, SD-активность),
// PL10 (зелёный, alive).
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

// PL10 (зелёный) = «проц жив»: 1 = горит (HIGH-active). Мигалка живёт на
// CPU1 (led_heartbeat_cpu1 вызывается из tft_core_main каждые 30 мс).
// Вынесена на R_PIO специально: CPU1 больше НЕ пишет в PA_DAT для LED,
// что сокращает окно RMW-гонки с Sega-падом (PA11/PA12) и тачем (PA21).
void led_set(int on) {
    if (on) PL_DAT |= (1u << 10);
    else    PL_DAT &= ~(1u << 10);
    mb();
}

// Индикатор «проц жив» — вызывать с CPU1 (tft_core_main, раз в ~30 мс).
// Мигает PL10: 0.5 с горит / 0.5 с гаснет. Не зависит от того, что делает
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

// PA15 (красный) = SD-активность: 1 = горит (HIGH-active).
// r703: управление PA_DAT (весь порт A в рантайме) перенесено на CPU2. core0
// НЕ пишет PA_DAT напрямую (была RMW-гонка с CPU1 по PA21/PA2 — с r703 порт A
// в рантайме трогает только CPU2, гонок нет). led_sd_on/off ставят флаг в
// coherent-shadow; CPU2 (audio_core.c, цикл звука) применяет его к регистру.
//
// Маска рантайм-битов порта A, которыми владеет CPU2. Остальные биты PA
// (PA2/PA21/PA1 — TFT/тач init, PA18-20 I2S init) настраиваются core0 в init
// ДО старта CPU2 и CPU2 не трогает (pa_dat_apply_desired сохраняет их).
#define PA_RUNTIME_BITS  ((1u << 10) | (1u << 15))   // PA10 SD-усилка, PA15 SD-LED

// Шадоу желаемого состояния рантайм-битов PA_DAT. В .coherent (uncached):
//  - core0 пишет биты через pa_dat_request (обычный store, uncached);
//  - CPU2 читает g_pa_shadow и применяет К РЕГИСТРУ pa_dat_apply_desired,
//    сохраняя не-рантайм биты (читать PA_DAT, заместить только PA_RUNTIME_BITS).
static volatile uint32_t g_pa_shadow __attribute__((section(".coherent"), aligned(4))) = 0;
// 1 = CPU2 поднят и владеет PA_DAT (Применяет shadow в цикле звука). core0 тогда
// не пишет регистр сам — только shadow. До старта CPU2 core0 применяет напрямую.
// Пишется core0 (main) один раз после h3_cpu_start(2), читается core0/CPU2.
static volatile int g_pa_cpu2_owner = 0;

void pa_dat_set_owner_cpu2(int on) { g_pa_cpu2_owner = on ? 1 : 0; __asm volatile("dmb st" ::: "memory"); }

// Вызывает CPU2 раз в цикле звука (audio_core.c): применяет желаемое значение
// рантайм-битов к PA_DAT, сохраняя остальные (init TFT/I2S) не тронутыми.
// Раз за цикл долива (~48 кГц/24) достаточно для PA15 (SD-всплески) без
// отдельных прерываний — звук не рвётся (нет ISR в hot path долива).
void pa_dat_apply_desired(void) {
    uint32_t desired = g_pa_shadow & PA_RUNTIME_BITS;   // только рантайм-биты
    uint32_t cur = PA_DAT;                              // остальные биты не трогаем
    cur = (cur & ~PA_RUNTIME_BITS) | desired;
    PA_DAT = cur;
    mb();
}

// Для core0: применить shadow сейчас, если CPU2 ещё НЕ владеет PA_DAT.
// (При владении CPU2 это делает сам в цикле — двойное применение безвредно, но
// лишний RMW от core0 не нужен; сохраняем инвариант «в рантайме регистр пишет
// только одна сторона».)
int pa_dat_apply_if_core0(void) {
    if (g_pa_cpu2_owner) return 0;
    pa_dat_apply_desired();
    return 1;
}

int pa_dat_request(int bit, int level) {
    if (bit < 0 || bit > 31) return -1;
    if (level) g_pa_shadow |=  (1u << bit);
    else       g_pa_shadow &= ~(1u << bit);
    // Барьер: core0 публикует shadow ДО того, как CPU2 прочитает и применит.
    __asm volatile("dmb st" ::: "memory");
    return 0;
}
void led_sd_on(void)  { pa_dat_request(15, 1); pa_dat_apply_if_core0(); }
void led_sd_off(void) { pa_dat_request(15, 0); pa_dat_apply_if_core0(); }
void led_sd_toggle(void){ g_pa_shadow ^= (1u << 15); __asm volatile("dmb st" ::: "memory"); pa_dat_apply_if_core0(); }