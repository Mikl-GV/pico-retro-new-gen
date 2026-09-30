// btn_pad.c — прямой 8-битовый PCF8574 (TWI0, bit-bang PA11/PA12).
// ОТДЕЛЬНЫЙ модуль: никак не лезем в протокол скана sega_pad.c.
// Карта кнопок (активный низ — кнопка замыкает линию на GND):
//   B0=Up B1=Left B2=Right B3=Down B4=A B5=B B6=Start B7=Select(Coin).
// Возвращает маску как sega_pad_scan (UP=1 DOWN=2 LEFT=4 RIGHT=8 A=0x10
// B=0x20 START=0x80 X=0x100 для Select/Coin).
//
// r0.390 (S2): низкоуровневый bit-bang ОБЩИЙ с sega_pad.c (pcf8574_bb.h) —
// одна реализация, «один поток». btn-скан выполняет ТОЛЬКО read-транзакции
// (start + addr|R + данные + stop): выходы PCF8574 не меняются, поэтому
// параллельно с Sega 6-btn-протоколом (TH на P7) он не конфликтует.
//
// r0.395 (адрес): кнопочная плата может стоять ЛИБО на 0x20 (один PCF8574
// на шине — Sega-джой и кнопки «или-или»), ЛИБО на 0x27 (второй PCF8574
// рядом с джойстиком на 0x20). btn_pad_dbg_init() сканирует оба адреса,
// выбирает рабочий (0x27 приоритетнее — 0x20 обычно занят джойстиком),
// включает подтяжки и печатает результат в UART.
//
// Подтяжки: PCF8574 — quasi-bidirectional порты: «вход» с подтяжкой = выход
// в 1. Кнопки активны в 0, поэтому один раз пишем 0xFF (все линии в 1).
// Для Sega-пада на 0x20 это то же состояние TH=HIGH idle, которое оставляет
// sega_pad_scan() в конце скана, — лишних фронтов TH нет.
#include <stdint.h>
#include "btn_pad.h"
#include "sega_pad.h"
#include "pcf8574_bb.h"

extern int printf(const char* fmt, ...);   // UART-отладка (PAD: ...)

// Рабочий адрес кнопочной платы (0 = не найдена).
static int btn_i2c_w = 0;   // адрес записи: 0x40 (0x20) / 0x4E (0x27)
static int btn_i2c_r = 0;   // адрес чтения: 0x41 (0x20) / 0x4F (0x27)
static int btn_pad_inited = 0;
static unsigned g_btn_reprobe = 0;   // счётчик неудач → ре-опрос (плата «горячего» подключения)

// Чтение одного байта из PCF8574 по выбранному адресу.
static int btn_pad_read(uint8_t* out) {
    if (!btn_i2c_r) return 0;
    i2c_start();
    if (i2c_write_byte((uint8_t)btn_i2c_r)) { i2c_stop(); return 0; }
    *out = i2c_read_byte(1);
    i2c_stop();
    return 1;
}

// r0.395: отладка инициализации кнопок по I2C (зовётся из main.c на старте).
// Сканирует 0x20 и 0x27; verbose=1 — печать в UART, verbose=0 — тихий
// ре-опрос (если плату подключили уже после загрузки).
static int btn_pad_probe(int verbose) {
    int a20, a27;

    i2c_start(); a20 = (i2c_write_byte(PCF8574_R) == 0); i2c_stop();
    i2c_start(); a27 = (i2c_write_byte(0x4F) == 0);      i2c_stop();

    if (verbose)
        printf("PAD: i2c scan: 0x20=%s 0x27=%s\n", a20 ? "YES" : "no", a27 ? "YES" : "no");

    if (a27) {
        btn_i2c_w = 0x4E; btn_i2c_r = 0x4F;
        if (verbose)
            printf("PAD: buttons -> 0x27 (Sega-джой остаётся на 0x20)\n");
    } else if (a20) {
        btn_i2c_w = PCF8574_W; btn_i2c_r = PCF8574_R;
        if (verbose)
            printf("PAD: buttons -> 0x20 (общая шина с джоем: и-или)\n");
    } else {
        btn_i2c_w = 0; btn_i2c_r = 0;
        if (verbose)
            printf("PAD: buttons NOT found\n");
        btn_pad_inited = 1;
        return 0;
    }

    // Подтяжки: все линии в 1 (активный-0). Один раз, не в каждом скане.
    i2c_start();
    int ok = i2c_write_byte((uint8_t)btn_i2c_w);
    if (!ok) ok = i2c_write_byte(0xFF);
    i2c_stop();
    if (verbose)
        printf("PAD: pull-ups 0xFF -> addr=0x%02X %s\n", (unsigned)btn_i2c_r,
               ok ? "FAIL(NACK)" : "OK");
    if (ok) { btn_i2c_w = 0; btn_i2c_r = 0; }

    btn_pad_inited = 1;
    return btn_i2c_r != 0;
}

int btn_pad_dbg_init(void) {
    return btn_pad_probe(1);
}

uint16_t btn_pad8_scan(void) {
    uint8_t r;

    if (!btn_pad_inited) btn_pad_probe(1);
    if (!btn_i2c_r) {
        // r0.395: платы не было — тихо переспрашиваем (раз в ~1000 вызовов),
        // чтобы кнопки в меню ожили после подключения без перезагрузки.
        if (++g_btn_reprobe >= 1000) { g_btn_reprobe = 0; btn_pad_inited = 0; }
        return 0;
    }
    g_btn_reprobe = 0;

    if (!btn_pad_read(&r)) return 0;
    uint16_t pad = 0;
    if (!(r & 0x01)) pad |= 0x0001;   // B0 Up
    if (!(r & 0x02)) pad |= 0x0004;   // B1 Left
    if (!(r & 0x04)) pad |= 0x0008;   // B2 Right
    if (!(r & 0x08)) pad |= 0x0002;   // B3 Down
    if (!(r & 0x10)) pad |= 0x0010;   // B4 A
    if (!(r & 0x20)) pad |= 0x0020;   // B5 B
    if (!(r & 0x40)) pad |= 0x0080;   // B6 Start
    if (!(r & 0x80)) pad |= 0x0100;   // B7 Select/Coin
    return pad;
}

uint16_t pad_scan_combined(void) {
    return sega_pad_scan() | btn_pad8_scan();
}