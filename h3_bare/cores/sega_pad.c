// sega_pad.c — Sega Mega Drive 6-button геймпад через PCF8574@0x20 (I2C).
//
// Разводка (TWI0, bit-bang): PA11=SCL, PA12=SDA.
// PCF8574 (адрес 0x20), раскладка СЕГА (все линии данных — активный низ):
//   P0 = D0  (Up)          — всегда
//   P1 = D1  (Down)        — всегда
//   P2 = D2  (Left / B / Z / Mode)   — зависит от фазы SELECT
//   P3 = D3  (Right / C / Y / X)     — зависит от фазы SELECT
//   P4 = D4  (A)           — всегда
//   P5 = D5  (Start)       — всегда
//   P6 = не подключён
//   P7 = TH (SELECT)       — выход 0/1
//
// Фазы чтения:
//   TH=1 (первое чтение): P2=Left  P3=Right
//   TH=0 (второе чтение): P2=B     P3=C
//   после 3 циклов переключения TH — счётчик чипа активирует 6-btn:
//   rb2 (TH=1): P2=Z     P3=Y
//   rb3 (TH=0): P2=Mode  P3=X
//
// Маска пада (как GPGX input.pad):
//   UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 C=0x40
//   START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
//
// ВАЖНО: PCF8574 обновляет выходные защёлки ТОЛЬКО на условии STOP. Repeated
// START не триггерит обновление — поэтому каждая фаза SELECT завершается
// отдельным STOP. Холостые циклы (2 и 3) не читают данные — только пишут
// SELECT со STOP (тик счётчика чипа). После записи SELECT — пауза 15 мкс
// (установление фронта TH через DE-9). Весь скан ~300 мкс << 1.5 мс лимита.

#include <stdint.h>
#include <string.h>
#include "sega_pad.h"
#include "h3.h"
#include "h3_hs_timer.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

// ---- PA11 = SCL, PA12 = SDA (GPIO на TWI0) ----
#define SCL_PIN  11
#define SDA_PIN  12

#define PA_BASE  0x01C20800u
#define PA_CFG1  (*(volatile uint32_t*)(PA_BASE + 0x04u))
#define PA_DAT   (*(volatile uint32_t*)(PA_BASE + 0x10u))

static inline void pa_scl_out(int v) {
    uint32_t cfg = PA_CFG1;
    int shift = (SCL_PIN % 8) * 4;
    if (v) { PA_DAT |=  (1u << SCL_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    else   { PA_DAT &= ~(1u << SCL_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    PA_CFG1 = cfg;
}

static inline void pa_sda_out(int v) {
    uint32_t cfg = PA_CFG1;
    int shift = (SDA_PIN % 8) * 4;
    if (v) { PA_DAT |=  (1u << SDA_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    else   { PA_DAT &= ~(1u << SDA_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    PA_CFG1 = cfg;
}

static inline void pa_sda_in(void) {
    uint32_t cfg = PA_CFG1;
    cfg &= ~(0xFu << ((SDA_PIN % 8) * 4));
    PA_CFG1 = cfg;
}

static inline int pa_sda_read(void) { return (PA_DAT & (1u << SDA_PIN)) ? 1 : 0; }

// Полупериод SCL. 130 тиков HS-таймера (100 МГц) = 1.3 мкс → SCL ~384 кГц.
static inline void t_half(void) {
    const uint32_t t0 = H3_HS_TIMER->CURNT_LO;
    while ((t0 - H3_HS_TIMER->CURNT_LO) < 130) {
        __asm__ volatile("nop");
    }
}

static void i2c_start(void) { pa_sda_out(1); pa_scl_out(1); t_half();
                              pa_sda_out(0); t_half(); pa_scl_out(0); t_half(); }

static void i2c_stop(void)  { pa_sda_out(0); pa_scl_out(1); t_half();
                              pa_sda_out(1); t_half(); }

// Возвращает 0 = ACK, 1 = NACK
static int i2c_write_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        pa_sda_out((b >> i) & 1); t_half();
        pa_scl_out(1); t_half(); pa_scl_out(0); t_half();
    }
    pa_sda_in(); t_half();
    pa_scl_out(1); t_half();
    int ack = pa_sda_read();
    pa_scl_out(0); t_half();
    return ack;
}

// last=1 — NACK после последнего байта чтения
static uint8_t i2c_read_byte(int last) {
    uint8_t b = 0;
    pa_sda_in();
    for (int i = 7; i >= 0; i--) {
        pa_scl_out(1); t_half();
        if (pa_sda_read()) b |= (1u << i);
        pa_scl_out(0); t_half();
    }
    pa_sda_out(last ? 1 : 0); t_half();
    pa_scl_out(1); t_half(); pa_scl_out(0); t_half();
    pa_sda_in();
    return b;
}

#define PCF8574_W  0x40
#define PCF8574_R  0x41

// Записать SELECT (TH=1/0) в PCF8574 со STOP — обновление выходов.
static int pcf_select(uint8_t sel) {
    i2c_start();
    if (i2c_write_byte(PCF8574_W)) { i2c_stop(); return 0; }
    if (i2c_write_byte(sel))       { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

// Прочитать PCF8574.
static int pcf_read(uint8_t* out) {
    i2c_start();
    if (i2c_write_byte(PCF8574_R)) { i2c_stop(); return 0; }
    *out = i2c_read_byte(1);
    i2c_stop();
    return 1;
}

// Пауза установления после записи SELECT: фронт TH проходит через DE-9
// (1-2 м провод), мультиплексор чипа Sega переключается за ~2 мкс, но
// по разным источникам — до 10-15 мкс. Даём 15 мкс для надёжности.
static inline void th_settle(void) {
    const uint32_t t0 = H3_HS_TIMER->CURNT_LO;
    while ((t0 - H3_HS_TIMER->CURNT_LO) < 1500) {}  // 15 мкс
}

static uint8_t g_raw[3] = {0xFF, 0xFF, 0xFF};
static uint32_t g_scan_us = 0;
static uint32_t g_status = 0;

void sega_pad_get_raw(uint8_t out[3]) {
    out[0] = g_raw[0]; out[1] = g_raw[1]; out[2] = g_raw[2];
}

uint32_t sega_pad_get_scan_us(void) { return g_scan_us; }
uint32_t sega_pad_get_status(void)  { return g_status; }

int sega_pad_init(void) {
    pa_scl_out(1);
    pa_sda_in();
    const uint32_t t0 = H3_HS_TIMER->CURNT_LO;
    while ((t0 - H3_HS_TIMER->CURNT_LO) < 100000) {}

    // PCF8574: P0..P6 = 1 (вход), P7 = 1 (TH=1, idle)
    int ok = pcf_select(0xFF);
    printf("sega_pad: init %s\n", ok ? "OK" : "FAIL");
    return ok;
}

// Полный скан 6-кнопочного геймпада. Каждая фаза: запись SELECT → STOP
// (обновление TH на PCF8574) → пауза → чтение/следующая запись.
// Холостые циклы 2-3: только запись SELECT (тик счётчика), без чтения.
uint16_t sega_pad_scan(void) {
    uint8_t r;
    uint16_t pad = 0;
    uint32_t t0 = h3_hs_timer_lo_us();

    g_status = 0;

    // --- ЦИКЛ 1, TH=1: крестовина (D0-D3) + B/C (TL/TR) ---
    if (!pcf_select(0xFF)) return 0;    // TH=1
    th_settle();
    if (!pcf_read(&r)) return 0;        g_raw[0] = r;
    if (!(r & 0x01)) pad |= 0x01;  // Up
    if (!(r & 0x02)) pad |= 0x02;  // Down
    if (!(r & 0x04)) pad |= 0x04;  // Left
    if (!(r & 0x08)) pad |= 0x08;  // Right
    if (!(r & 0x10)) pad |= 0x20;  // B
    if (!(r & 0x20)) pad |= 0x40;  // C

    // --- ЦИКЛ 1, TH=0: A/Start (TL/TR) + маркер геймпада ---
    if (!pcf_select(0x7F)) return 0;    // TH=0
    th_settle();
    if (!pcf_read(&r)) return 0;        g_raw[1] = r;
    if (!(r & 0x10)) pad |= 0x10;  // A
    if (!(r & 0x20)) pad |= 0x80;  // Start
    if (!(r & 0x04) && !(r & 0x08)) g_status |= SEGA_STATUS_PAD;

    // --- ЦИКЛ 2: холостая прокрутка (TH=1, TH=0) — только SELECT, без чтения ---
    if (!pcf_select(0xFF)) return 0;    // TH=1
    if (!pcf_select(0x7F)) return 0;    // TH=0

    // --- ЦИКЛ 3: холостая прокрутка (TH=1, TH=0) — только SELECT ---
    if (!pcf_select(0xFF)) return 0;    // TH=1
    if (!pcf_select(0x7F)) return 0;    // TH=0

    // --- ЦИКЛ 4, TH=1: X/Y/Z/Mode (D0-D3) + B/C (TL/TR) ---
    if (!pcf_select(0xFF)) return 0;    // TH=1
    th_settle();
    if (!pcf_read(&r)) return 0;        g_raw[2] = r;
    if (!(r & 0x01)) pad |= 0x400;  // Z
    if (!(r & 0x02)) pad |= 0x200;  // Y
    if (!(r & 0x04)) pad |= 0x100;  // X
    if (!(r & 0x08)) pad |= 0x800;  // Mode
    if (!(r & 0x10)) pad |= 0x20;   // B
    if (!(r & 0x20)) pad |= 0x40;   // C

    // --- Сброс: TH=0, затем idle TH=1 ---
    if (!pcf_select(0x7F)) return 0;
    if (!pcf_select(0xFF)) return 0;

    g_status |= SEGA_STATUS_ACK;
    g_scan_us = h3_hs_timer_lo_us() - t0;
    return pad;
}

void sega_pad_dump(uint16_t pad) {
    static const struct { uint16_t bit; const char* name; } map[] = {
        { 0x001, "Up" }, { 0x002, "Down" }, { 0x004, "Left" }, { 0x008, "Right" },
        { 0x010, "A" }, { 0x020, "B" }, { 0x040, "C" }, { 0x080, "Start" },
        { 0x100, "X" }, { 0x200, "Y" }, { 0x400, "Z" }, { 0x800, "Mode" },
    };
    char line[128];
    int pos = 0;
    if (!pad) {
        const char* s = "(none)";
        while (*s && pos < 126) line[pos++] = *s++;
    } else {
        int first = 1;
        for (int i = 0; i < 12; i++) {
            if (pad & map[i].bit) {
                if (!first) line[pos++] = ' ';
                first = 0;
                const char* s = map[i].name;
                while (*s && pos < 126) line[pos++] = *s++;
            }
        }
    }
    line[pos] = 0;
    uart_puts(line);
    uart_puts("\n");
}