// сеga_pad.c — Sega Mega Drive 6-button геймпад через PCF8574@0x20 (I2C).
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

// Настроить пин PA11/PA12 как выход (1) или вход-высокий импеданс (0)
static inline void pa_set_dir(int pin, int output) {
    int shift = (pin % 8) * 4;
    if (output) PA_CFG1 = (PA_CFG1 & ~(0xFu << shift)) | (0x1u << shift);
    else        PA_CFG1 = (PA_CFG1 & ~(0xFu << shift));          // input
    __asm volatile("dsb sy" ::: "memory");
}

static inline void scl_hi(void) { pa_set_dir(SCL_PIN, 0); }       // high-Z, подтяжка вверх
static inline void scl_lo(void) { PA_DAT &= ~(1u << SCL_PIN); pa_set_dir(SCL_PIN, 1); }
static inline void sda_hi(void) { pa_set_dir(SDA_PIN, 0); }
static inline void sda_lo(void) { PA_DAT &= ~(1u << SDA_PIN); pa_set_dir(SDA_PIN, 1); }
static inline int  sda_read(void) { return (PA_DAT & (1u << SDA_PIN)) ? 1 : 0; }

static void i2c_delay(void) { udelay(1); }   // ~400 кГц (PCF8574 тянет)

static void i2c_start(void) {
    sda_hi(); scl_hi(); i2c_delay();
    sda_lo(); i2c_delay();
    scl_lo(); i2c_delay();
}

static void i2c_stop(void) {
    sda_lo(); i2c_delay();
    scl_hi(); i2c_delay();
    sda_hi(); i2c_delay();
}

// Возвращает 0 = ACK, 1 = NACK
static int i2c_write_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) sda_hi(); else sda_lo();
        i2c_delay();
        scl_hi(); i2c_delay();
        scl_lo(); i2c_delay();
    }
    // ACK bit
    sda_hi(); i2c_delay();
    scl_hi(); i2c_delay();
    int ack = sda_read();
    scl_lo(); i2c_delay();
    return ack;
}

// last=1 — NACK после последнего байта чтения
static uint8_t i2c_read_byte(int last) {
    uint8_t b = 0;
    sda_hi();
    for (int i = 7; i >= 0; i--) {
        scl_hi(); i2c_delay();
        if (sda_read()) b |= (1u << i);
        scl_lo(); i2c_delay();
    }
    // ACK/NACK
    if (last) sda_hi(); else sda_lo();
    i2c_delay();
    scl_hi(); i2c_delay();
    scl_lo(); i2c_delay();
    sda_hi();
    return b;
}

#define PCF8574_W  0x40   // 0x20 << 1 (write)
#define PCF8574_R  0x41   // 0x20 << 1 | 1 (read)

// Записать байт в PCF8574. Возвращает 1 при успехе.
static int pcf_write(uint8_t val) {
    i2c_start();
    if (i2c_write_byte(PCF8574_W)) { i2c_stop(); return 0; }
    if (i2c_write_byte(val))       { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

// Прочитать байт из PCF8574. Возвращает 0 при ошибке (выходной param).
static int pcf_read(uint8_t* out) {
    i2c_start();
    if (i2c_write_byte(PCF8574_R)) { i2c_stop(); return 0; }
    *out = i2c_read_byte(1);
    i2c_stop();
    return 1;
}

// Сырые чтения PCF8574 за скан: raw[0]=TH1(D-Pad+B+C), raw[1]=TH0(A+Start), raw[2]=ЦИКЛ4 TH1(Z+Y+X+Mode+B+C)
static uint8_t g_raw[3] = {0xFF, 0xFF, 0xFF};

// Время последнего скана в микросекундах (замеряет sega_pad_scan)
static uint32_t g_scan_us = 0;

// Статус последнего скана (битовые флаги)
static uint32_t g_status = 0;

// Доступ к сырым чтениям (для UART-диагностики)
void sega_pad_get_raw(uint8_t out[3]) {
    out[0] = g_raw[0]; out[1] = g_raw[1]; out[2] = g_raw[2];
}

uint32_t sega_pad_get_scan_us(void) {
    return g_scan_us;
}

uint32_t sega_pad_get_status(void) {
    return g_status;
}

int sega_pad_init(void) {
    // PA11/PA12 → входы (высокий импеданс, линии подтянуты) + стартовая запись
    pa_set_dir(SCL_PIN, 0);
    pa_set_dir(SDA_PIN, 0);
    udelay(1000);   // дать линиям установиться (pull-up)
    // PCF8574: P0..P6 = 1 (вход), P7 = 1 (TH=1, старт)
    int ok = pcf_write(0xFF);
    printf("sega_pad: init %s (PA11=SCL PA12=SDA)\n", ok ? "OK" : "FAIL");
    return ok;
}

// Полный скан 6-кнопочного геймпада Sega Mega Drive — классический протокол
// (прямые линии геймпада через PCF8574, как в рабочем Arduino-адаптере):
//
//   P0 = D0 (Up / Z)      P1 = D1 (Down / Y)
//   P2 = D2 (Left / X)    P3 = D3 (Right / Mode)
//   P4 = D4 (TL = B / A)  P5 = D5 (TR = C / Start)
//   P7 = TH (SELECT, выход)
//
// Алгоритм (фазы SELECT): T_US ~150 мкс после переключения до чтения,
// весь цикл сброса — LOW потом HIGH (как в скетче):
//
//   ЦИКЛ 1  TH=1: Up Down Left Right  B(TL)  C(TR)
//   ЦИКЛ 1  TH=0: A(TL)  Start(TR)
//   ЦИКЛ 2  TH=1, TH=0          (холостые прокрутки счётчика)
//   ЦИКЛ 3  TH=1, TH=0          (холостые прокрутки счётчика)
//   ЦИКЛ 4  TH=1: Z(P0) Y(P1) X(P2) Mode(P3)  + B(TL) C(TR)
//   СБРОС   TH=0, затем TH=1 (idle)
//
// Маска (как GPGX): UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08
//   A=0x10 B=0x20 C=0x40 START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
// Полный скан 6-кнопочного геймпада Sega Mega Drive — классический протокол
// (прямые линии геймпада через PCF8574, как в рабочем Arduino-адаптере):
//
//   P0 = D0 (Up / Z)      P1 = D1 (Down / Y)
//   P2 = D2 (Left / X)    P3 = D3 (Right / Mode)
//   P4 = D4 (TL = B / A)  P5 = D5 (TR = C / Start)
//   P7 = TH (SELECT, выход)
//
// Алгоритм (фазы SELECT): T_US ~150 мкс после переключения до чтения,
// весь цикл сброса — LOW потом HIGH (как в скетче):
//
//   ЦИКЛ 1  TH=1: Up Down Left Right  B(TL)  C(TR)
//   ЦИКЛ 1  TH=0: A(TL)  Start(TR)
//   ЦИКЛ 2  TH=1, TH=0          (холостые прокрутки счётчика)
//   ЦИКЛ 3  TH=1, TH=0          (холостые прокрутки счётчика)
//   ЦИКЛ 4  TH=1: Z(P0) Y(P1) X(P2) Mode(P3)  + B(TL) C(TR)
//   СБРОС   TH=0, затем TH=1 (idle)
//
// ВАЖНО: последовательность фаз и «пустые кадры» (холостые прокрутки
// счётчика чипа) нарушать нельзя — иначе десинхронизация 6-кнопочного
// режима (X/Y/Z/Mode начинают читаться не в свой цикл). Поэтому при
// ошибке I2C мы НЕ прерываем протокол: все фазы выполняются всегда,
// сбойные чтения дают 0xFF (все кнопки отпущены), но SEGA_STATUS_ACK
// не ставится. Принимающая сторона (usb_input_poll/usb_pad_just_pressed)
// видит отсутствие ACK и игнорирует скан, не трогая g_pad_prev —
// ложных срабатываний нет, а фазы чипа остаются синхронными.
//
// Маска (как GPGX): UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08
//   A=0x10 B=0x20 C=0x40 START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
uint16_t sega_pad_scan(void) {
    uint8_t r;
    uint16_t pad = 0;
    uint32_t t0 = h3_hs_timer_lo_us();
    int      i2c_ok = 1;   // станет 0 при любой ошибке шины

    // Задержка после смены TH (T_US) — критична для счётчика чипа
    #define PAD_TUS() udelay(20)

    g_status = 0;

    // --- ЦИКЛ 1, TH=1: крестовина (D0-D3) + B/C (TL/TR) ---
    if (!pcf_write(0xFF)) { i2c_ok = 0; } else { if (!pcf_read(&r)) { i2c_ok = 0; r = 0xFF; } }
    g_raw[0] = r;
    if (!(r & 0x01)) pad |= 0x01;  // Up    (P0)
    if (!(r & 0x02)) pad |= 0x02;  // Down  (P1)
    if (!(r & 0x04)) pad |= 0x04;  // Left  (P2)
    if (!(r & 0x08)) pad |= 0x08;  // Right (P3)
    if (!(r & 0x10)) pad |= 0x20;  // B     (P4/TL)
    if (!(r & 0x20)) pad |= 0x40;  // C     (P5/TR)

    // --- ЦИКЛ 1, TH=0: A/Start (TL/TR) + маркер D2/D3=0 (геймпад подключён) ---
    if (!pcf_write(0x7F)) { i2c_ok = 0; } else { if (!pcf_read(&r)) { i2c_ok = 0; r = 0xFF; } }
    PAD_TUS();
    g_raw[1] = r;
    if (!(r & 0x10)) pad |= 0x10;  // A     (P4/TL)
    if (!(r & 0x20)) pad |= 0x80;  // Start (P5/TR)
    if (!(r & 0x04) && !(r & 0x08)) g_status |= SEGA_STATUS_PAD;  // маркер геймпада

    // --- ЦИКЛ 2 и 3: холостые прокрутки счётчика чипа (пустые кадры) ---
    // Каждая пара TH1/TH0 продвигает внутренний счётчик 6-кнопочного
    // режима. Пропускать их нельзя — иначе сдвинутся фазы.
    // ЦИКЛ 2
    if (!pcf_write(0xFF)) { i2c_ok = 0; } PAD_TUS();
    if (!pcf_write(0x7F)) { i2c_ok = 0; } PAD_TUS();
    // ЦИКЛ 3
    if (!pcf_write(0xFF)) { i2c_ok = 0; } PAD_TUS();
    if (!pcf_write(0x7F)) { i2c_ok = 0; } PAD_TUS();

    // --- ЦИКЛ 4, TH=1: X/Y/Z/Mode (D0-D3) + B/C (TL/TR) ---
    if (!pcf_write(0xFF)) { i2c_ok = 0; } else { if (!pcf_read(&r)) { i2c_ok = 0; r = 0xFF; } }
    PAD_TUS();
    g_raw[2] = r;
    if (!(r & 0x01)) pad |= 0x400;  // Z    (P0)
    if (!(r & 0x02)) pad |= 0x200;  // Y    (P1)
    if (!(r & 0x04)) pad |= 0x100;  // X    (P2)
    if (!(r & 0x08)) pad |= 0x800;  // Mode (P3)
    if (!(r & 0x10)) pad |= 0x20;   // B    (P4/TL)
    if (!(r & 0x20)) pad |= 0x40;   // C    (P5/TR)

    // --- Сброс: TH=0, затем idle TH=1 (как в скетче: LOW->HIGH + пауза) ---
    if (!pcf_write(0x7F)) { i2c_ok = 0; } PAD_TUS();
    if (!pcf_write(0xFF)) { i2c_ok = 0; } PAD_TUS();
    udelay(100);

    #undef PAD_TUS

    if (i2c_ok)
        g_status |= SEGA_STATUS_ACK;   // иначе: сбой — потребитель игнорирует
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