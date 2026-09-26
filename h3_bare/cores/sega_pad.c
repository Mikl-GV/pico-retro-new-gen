// sega_pad.c — Sega Mega Drive 6-button геймпад через PCF8574@0x20 (I2C).
//
// Разводка (TWI0, bit-bang): PA11=SCL, PA12=SDA.
// PCF8574 (адрес 0x20), раскладка СЕГА (все линии данных — активный низ):
//   P0 = D0  (Up)          — всегда (в 6-btn фазе = Z)
//   P1 = D1  (Down)        — всегда (в 6-btn фазе = Y)
//   P2 = D2  (Left / B / X)—— зависит от фазы SELECT
//   P3 = D3  (Right / C / Mode) — зависит от фазы SELECT
//   P4 = D4  (A)           — всегда
//   P5 = D5  (Start)       — всегда
//   P6 = не подключён
//   P7 = TH (SELECT)       — выход 0/1
//
// Протокол чтения (эталон: gen_hw.txt / Sega 315-5638 ASIC):
//   Консоль и пад общаются пульсацией SELECT (TH). В 3-button режиме пад
//   отвечает крестовиной при TH=1 и A/Start при TH=0. Чтобы разблокировать
//   старшие кнопки, консоль делает полную последовательность уровней TH:
//
//     ур.1 TH=1 : крестовина + B/C          (чтение)
//     ур.2 TH=0 : A/Start                   (чтение)
//     ур.3 TH=1 : холостой
//     ур.4 TH=0 : холостой
//     ур.5 TH=1 : холостой
//     ур.6 TH=0 : D0-D3 принудительно 0000  (маркер 6-btn!)
//     ур.7 TH=1 : Z/Y/X/Mode + B/C          (чтение)
//     ур.8 TH=0 : сброс в idle
//
//   Линии D0-D3 общие: фаза 1 читает крестовину, фаза 7 — Z/Y/X/Mode.
//   Поэтому старшая кнопка выдаётся ТОЛЬКО если её линия не занята
//   крестовиной в фазе 1 (clash-защита). Иначе при нажатой крестовине
//   фаза 7 давала «фантом»: Right→Mode, Left→X, Down→Y, Up→Z (а Mode в
//   эмуляторах уходит в Select — «вправо выбирает игру» и т.п.).
//   Если на ур.6 маркер D0-D3 != 0000 — пад не 6-button; XYZM не выдаём
//   (clash-защита дополнительно гасит «крестовину как старшие кнопки»).
//
//   Тайминги (проверено на железе):
//     - SCL ~370 кГц (t_half=130 тиков @97 МГц). Медленная I2C (~92 кГц) не
//       успевает «насчитать» переключения TH за окно чипа.
//     - Паузы между фазами TH = 20 мкс (th_settle/th_hold/th_idle).
//       ВАЖНО: весь детект должен уложиться в ~1.6 мс от первого up-edge
//       SELECT (allpinouts/retrosix; gbppr). При 50 мкс/фазу скан ~1.76 мс
//       выходил за окно — чип сбрасывался. 20 мкс дают ~1.4 мс.
//     - После скана TH возвращается в HIGH (idle, как у консоли) — иначе
//       следующий скан стартует с лишнего up-edge и счётчик сбивается.
//
//   PCF8574: обновляет выходы ТОЛЬКО на условии STOP. Каждая фаза SELECT —
//   отдельная I2C-транзакция со STOP (Repeated-START не подходит).
//
//   Маска пада (как GPGX input.pad):
//     UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 C=0x40
//     START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
//
//   ЗАЩЁЛКА на пакет: pad накапливается через |= — кнопка, пойманная хоть в
//   одной фазе, не снимается до конца скана (отсекает дребезг контакта).

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

// Полупериод SCL: 130 тиков @~97 МГц = ~1.3 мкс → SCL ~370 кГц.
// Медленнее (92 кГц) 6-btn детект не успевает за окно 1.6 мс ↔
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

// Паузы фаз детекта 6-btn. 20 мкс/фазу: весь скан ~1.4 мс — внутри окна 1.6 мс
// от первого up-edge SELECT (allpinouts/retrosix; gbppr). ВАЖНО: отсчёт через
// h3_hs_timer_lo_us() (64-бит) — по CURNT_LO (32 бита) раз в ~44 с пауза
// срабатывала мгновенно и детект срывался.
#define TH_SETTLE_US 20   // после записи SELECT (установление TH)
#define TH_HOLD_US   20   // после чтения (дать линиям устояться)
#define TH_IDLE_US   20   // между холостыми фазами счётчика
static inline void th_settle(void) {
    uint32_t t0 = h3_hs_timer_lo_us();
    while ((h3_hs_timer_lo_us() - t0) < TH_SETTLE_US) {}
}

static inline void th_hold(void) {
    uint32_t t0 = h3_hs_timer_lo_us();
    while ((h3_hs_timer_lo_us() - t0) < TH_HOLD_US) {}
}

static inline void th_idle(void) {
    uint32_t t0 = h3_hs_timer_lo_us();
    while ((h3_hs_timer_lo_us() - t0) < TH_IDLE_US) {}
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

// Полный скан 6-кнопочного геймпада (эталон Rosenberg/Thysell/SegaController).
//
//  SELECT-импульс №1: TH=1 читаем крестовину (D0-D3) + B/C (TL/TR)
//                     TH=0 читаем A/Start (TL/TR) + маркер геймпада (D2/D3=0)
//  SELECT-импульс №2: TH=1, TH=0 — холостые (счётчик чипа тикает)
//  SELECT-импульс №3: TH=1 — читаем X/Y/Z/Mode (D0-D3) + B/C (TL/TR)
//                     TH=0 — стоп/сброс (чип выходит из 6-btn режима)
//
// ВАЖНО: старшие кнопки приходят на HIGH ТРЕТЬЕГО импульса. Если сделать
// лишний 4-й импульс или слишком длинные паузы — чип не активирует 6-btn
// и на этом месте отдаёт крестовину (симптом «Mode на крестовине»).
// ЗАЩЁЛКА на пакет: pad накапливается через |= — кнопка, пойманная хоть в
// одной фазе, не снимается до конца скана (отсекает дребезг контакта).
uint16_t sega_pad_scan(void) {
    uint8_t r;
    uint8_t r_dpad = 0xFF;   // байт фазы 1 (крестовина): 0 = линия занята
    uint16_t pad = 0;
    uint32_t t0 = h3_hs_timer_lo_us();

    g_status = 0;

    // --- ИМПУЛЬС 1, TH=1: крестовина (D0-D3) + B/C (TL/TR) ---
    if (!pcf_select(0xFF)) return 0;    // TH=1
    th_settle();
    if (!pcf_read(&r)) return 0;
    g_raw[0] = r;
    r_dpad = r;
    th_hold();
    if (!(r & 0x01)) pad |= 0x01;  // Up
    if (!(r & 0x02)) pad |= 0x02;  // Down
    if (!(r & 0x04)) pad |= 0x04;  // Left
    if (!(r & 0x08)) pad |= 0x08;  // Right
    if (!(r & 0x10)) pad |= 0x20;  // B
    if (!(r & 0x20)) pad |= 0x40;  // C

    // --- ИМПУЛЬС 1, TH=0: A/Start (TL/TR) + маркер геймпада ---
    if (!pcf_select(0x7F)) return 0;    // TH=0
    th_settle();
    if (!pcf_read(&r)) return 0;
    g_raw[1] = r;
    th_hold();
    if (!(r & 0x10)) pad |= 0x10;  // A
    if (!(r & 0x20)) pad |= 0x80;  // Start
    if (!(r & 0x04) && !(r & 0x08)) g_status |= SEGA_STATUS_PAD;

    // --- ИМПУЛЬСЫ 2-4: полный 6-button детект (gen_hw.txt + факт с железа):
//     маркер D0-D3=0 на 6-й фазе (LOW), XYZ — на 7-й (HIGH) ---
//   1:H(read) 2:L(read) 3:H(idl) 4:L(idl) 5:H(idl) 6:L(marker!) 7:H(XYZ!) 8:L(reset)
    if (!pcf_select(0xFF)) return 0;    // TH=1 (ур.3, холостой)
    th_idle();
    if (!pcf_select(0x7F)) return 0;    // TH=0 (ур.4, холостой)
    th_idle();
    if (!pcf_select(0xFF)) return 0;    // TH=1 (ур.5, холостой)
    th_idle();
    if (!pcf_select(0x7F)) return 0;    // TH=0 (ур.6 — маркер)
    th_settle();
    if (!pcf_read(&r)) return 0;        // маркер 6-btn: D0-D3 должны быть 0000
    if ((r & 0x0F) == 0) g_status |= SEGA_STATUS_PAD6;   // r152: не выходим, а метим
    th_hold();

    // --- ур.7: TH=1, X/Y/Z/Mode (D0-D3) + B/C (TL/TR) ---
    if (!pcf_select(0xFF)) return 0;    // TH=1 (ур.7)
    th_settle();
    if (!pcf_read(&r)) return 0;
    g_raw[2] = r;
    th_hold();
    // ЛИНИИ D0-D3 ОБЩИЕ: фаза 1 = крестовина, фаза 7 = Z/Y/X/Mode.
    // Если крестовина держит линию (r_dpad bit = 0), в фазе 7 читается
    // ФАНТОМ старшей кнопки (Right→Mode, Left→X, Down→Y, Up→Z) —
    // реальная консоль различить их не может, поэтому не выдаём.
    if ((r_dpad & 0x01) && !(r & 0x01)) pad |= 0x400;  // Z
    if ((r_dpad & 0x02) && !(r & 0x02)) pad |= 0x200;  // Y
    if ((r_dpad & 0x04) && !(r & 0x04)) pad |= 0x100;  // X
    if ((r_dpad & 0x08) && !(r & 0x08)) pad |= 0x800;  // Mode
    if (!(r & 0x10)) pad |= 0x20;   // B (защёлка — уже стоит из ИМПУЛЬС 1)
    if (!(r & 0x20)) pad |= 0x40;   // C (защёлка)

    // --- ур.8: TH=0 — сброс в idle (чип выходит из 6-btn режима) ---
    if (!pcf_select(0x7F)) return 0;
    th_idle();
    // r151: idle-состояние пада = TH HIGH (как у консоли). Возвращаем TH=1,
    // иначе следующий скан стартует с фронтом 0→1 «первого up-edge Sel».
    pcf_select(0xFF);
    th_idle();

    g_status |= SEGA_STATUS_ACK;
    g_scan_us = h3_hs_timer_lo_us() - t0;
    return pad;
}