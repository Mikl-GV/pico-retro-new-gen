#ifndef BTN_PAD_H
#define BTN_PAD_H

#include <stdint.h>

// r0.389: прямой 8-битовый PCF8574@0x20 (кнопка на линию, активный низ).
// Раскладка (карта пользователя):
//   B0=Up B1=Left B2=Right B3=Down B4=A B5=B B6=Start B7=Select(Coin)
// Возвращает ту же битовую маску, что sega_pad_scan:
//   UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 START=0x80 X=0x100(Coin)
uint16_t btn_pad8_scan(void);

// Единый источник для эмуляторов/меню: Sega 6-btn OR 8-битовый пад.
// НЕ изменяет sega_pad_scan (он остаётся как есть).
uint16_t pad_scan_combined(void);

// r0.395/r0.411: работа с кнопочной платой I2C в verbose-режиме (лог в UART:
// «PAD: i2c scan: 0x20=… 0x27=…», выбор адреса, подтяжки). Зовётся из main.c
// на старте (бут-лог) и безопасно повторяется из первого скана. Возвращает
// 1, если плата найдена.
int btn_pad_dbg_init(void);

#endif