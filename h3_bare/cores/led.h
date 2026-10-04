// led.h
#ifndef LED_H
#define LED_H

void led_init(void);
void led_set(int on);
void led_heartbeat_cpu1(void);

void led_sd_on(void);
void led_sd_off(void);
void led_sd_toggle(void);

// r703: весь PA_DAT в рантайме пишет ТОЛЬКО CPU2 (см. led.c). core0 не делает
// RMW по PA_DAT напрямую — он обновляет желаемое значение (g_pa_shadow в
// .coherent), а CPU2 в цикле звука применяет его к регистру. pa_dat_request —
// единственная точка изменения PA_DAT со стороны core0/инициализатора.
int  pa_dat_request(int bit, int level);   // 1 = установить бит, 0 = снять
void pa_dat_apply_desired(void);           // вызывает CPU2 (audio_core.c) раз в цикле
// Один раз (main) после старта CPU2: 1 = CPU2 владеет PA_DAT, core0 не пишет регистр.
void pa_dat_set_owner_cpu2(int on);
// Для core0: применить shadow сейчас, если CPU2 ещё НЕ владеет PA_DAT. Возвращает
// 1, если регистр написан (CPU2 не владеет), иначе 0 (применит CPU2).
int  pa_dat_apply_if_core0(void);

#endif