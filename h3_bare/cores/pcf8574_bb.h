#ifndef PCF8574_BB_H
#define PCF8574_BB_H

#include <stdint.h>

// Низкоуровневый bit-bang I2C на PG9/PG8 (SCL/SDA) — ЕДИНАЯ реализация
// для sega_pad.c и btn_pad.c (r0.390, S2: «один поток», дубль драйвера убран).
// Функции живут в sega_pad.c (там же вся эмпирика таймингов, не трогать).
// r614: пины перенесены с PA11/PA12 (TWI0) на PG9/PG8 (порт G) — бит-банг
// больше не пишет в PA_DAT (делится с i2s-SD PA10 и CPU1-TFT PA21).
//
// ВАЖНО для btn-скана: кнопочный скан делает ТОЛЬКО read-транзакцию
// (start + addr|R + данные + stop) — выходы PCF8574 при этом НЕ меняются,
// поэтому параллельно с Sega 6-btn-протоколом (TH на P7) он не конфликтует.

void     i2c_t_half(void);          // полупериод SCL (~1.3 мкс @~97 МГц)
void     i2c_start(void);
void     i2c_stop(void);
int      i2c_write_byte(uint8_t b); // 0 = ACK, 1 = NACK
uint8_t  i2c_read_byte(int last);   // last=1 — NACK после последнего байта

// PCF8574: 0x40 write / 0x41 read (адрес 0x20 + R/W)
#define PCF8574_W  0x40
#define PCF8574_R  0x41

#endif