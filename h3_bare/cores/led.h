#ifndef LED_H
#define LED_H

void led_init(void);
void led_set(int on);
void led_heartbeat_cpu1(void);

void led_sd_on(void);
void led_sd_off(void);
void led_sd_toggle(void);

// r706: PA_DAT (порт A) в рантайме пишет ТОЛЬКО core0. Единственная точка
// управления битами PA10 (SD-усилка I2S) и PA15 (SD-LED) — pa_dat_set().
// CPU1 после r703 PA_DAT не трогает (CS тача на PC4), CPU2 порт A не пишет.
// (Откат r703: было через coherent-shadow + владелец CPU2, из-за чего PA10
// мог оставаться на земле после клика в меню — эмуляторы молчали.)
int  pa_dat_set(int bit, int level);   // 1 = установить бит, 0 = снять

#endif