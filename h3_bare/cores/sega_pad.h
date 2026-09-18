#ifndef SEGA_PAD_H
#define SEGA_PAD_H

#include <stdint.h>

// Инициализация: PA11=SCL, PA12=SDA (TWI0, bit-bang), питание PL5 — НЕ трогаем (U-Boot).
int  sega_pad_init(void);

// Сырые чтения PCF8574 за скан: raw[0]=TH1(D-Pad+B+C), raw[1]=TH0(A+Start), raw[2]=ЦИКЛ4 TH1(Z+Y+X+Mode+B+C)
void sega_pad_get_raw(uint8_t out[3]);

// Статус последнего скана (битовые флаги)
#define SEGA_STATUS_ACK   0x01   // все I2C-транзакции прошли (микросхема на шине)
#define SEGA_STATUS_PAD   0x02   // геймпад подключён (маркер D2/D3=0 в фазе TH0)
uint32_t sega_pad_get_status(void);

// Время последнего скана в микросекундах
uint32_t sega_pad_get_scan_us(void);

// Скан Sega 6-button через PCF8574@0x20.
// Два прохода: TH=1 (первичные) и TH=0 (вторичные).
// Возвращает битовую маску как в GPGX input.pad:
//   UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 C=0x40 START=0x80
//   X=0x100 Y=0x200 Z=0x400 MODE=0x800
// 0 = не удалось (нет ответа на шине).
uint16_t sega_pad_scan(void);

// Вывод нажатых кнопок в UART одной строкой.
void sega_pad_dump(uint16_t pad);

#endif