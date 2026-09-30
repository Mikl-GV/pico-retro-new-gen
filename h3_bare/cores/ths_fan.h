#ifndef THS_FAN_H
#define THS_FAN_H

#include <stdint.h>

// ths_fan.c — температура H3 (THS) + вентилятор с тахометром (FG).

// Инициализация: такты CCU (bus-ths), THS (один сенсор, формула
// 217000 − reg·1211/10 м°C), PA6=управление вентилятором, PA7=FG-вход.
// Вызывать на старте (main) до первого ths_fan_update.
void ths_fan_init(void);

// Единый тик: чтение THS (не чаще раза в 500 мс), гистерезис вентилятора,
// перепады FG и расчёт RPM. Вызывается из fb_flush() каждый кадр.
void ths_fan_update(void);

// Оверлей температуры в правом верхнем углу HDMI-FB (виден всегда,
// r0.415: без порога). Вызывается из fb_flush() сразу за ths_fan_update.
void ths_fan_overlay(void);

// Текущее состояние: температура в десятых °C (212 = 21.2°C), RPM (0 = стои).
void ths_fan_get(int* temp_c10, int* rpm);

// r0.415: температура в десятых °C без копий (для UART-диагностики).
int  ths_fan_temp_c10(void);

// Сигнал «перегрев» (>= 55°C) — для TFT/меню при желании.
int  ths_fan_overheated(void);

// Сырое чтение THS в м°C (0 = не инициализирован/нет данных).
int  ths_read_millideg(void);

#endif