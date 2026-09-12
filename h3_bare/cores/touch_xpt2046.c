// touch_xpt2046.c — драйвер сенсорной панели XPT2046 (7" HDMI LCD).
//
// Подключение через первые 26 пинов 40-pin разъёма (1:1 с RPi 3/4).
// Распиновка Orange Pi Lite (H3) — проверено по таблице разъёма:
//   Дисплей     OPI Lite пин   H3 GPIO
//   19 TP_SI    19              PC0  (SPI0_MOSI)
//   21 TP_SO    21              PC1  (SPI0_MISO)
//   22 TP_IRQ   22              PA2  (низкий = касание)
//   23 TP_SCK   23              PC2  (SPI0_CLK)
//   26 TP_CS    26              PA21 (касание, низкий = активно)
//
// SPI — bit-bang (не используем аппаратный контроллер), CS — любой GPIO.
#include "h3.h"
#include "touch.h"

// Порт C: PC0=MOSI, PC1=MISO, PC2=SCLK
#define C_CFG0  H3_PIO_PORTC->CFG0
#define C_DAT   H3_PIO_PORTC->DAT

// Порт A: PA2=IRQ, PA21=CS
#define A_CFG0  H3_PIO_PORTA->CFG0
#define A_CFG2  H3_PIO_PORTA->CFG2   // PA20..PA23
#define A_DAT   H3_PIO_PORTA->DAT

#define PIN_MOSI  0   // PC0
#define PIN_MISO  1   // PC1
#define PIN_SCLK  2   // PC2
#define PIN_IRQ   2   // PA2
#define PIN_CS   21   // PA21

static inline void cs_high(void)    { A_DAT |=  (1u << PIN_CS); }
static inline void cs_low(void)     { A_DAT &= ~(1u << PIN_CS); }
static inline void sclk_high(void)  { C_DAT |=  (1u << PIN_SCLK); }
static inline void sclk_low(void)   { C_DAT &= ~(1u << PIN_SCLK); }
static inline void mosi_high(void)  { C_DAT |=  (1u << PIN_MOSI); }
static inline void mosi_low(void)   { C_DAT &= ~(1u << PIN_MOSI); }
static inline int  miso_read(void)  { return (C_DAT >> PIN_MISO) & 1; }
static inline int  irq_read(void)   { return (A_DAT >> PIN_IRQ) & 1; }

// Конфигурация пинов:
//   PC0 (MOSI), PC2 (SCLK)  -> выход
//   PC1 (MISO), PA2 (IRQ)   -> вход
//   PA21 (CS)               -> выход
static void port_pins_cfg(void) {
    uint32_t c = C_CFG0;
    c &= ~(0xFu << (PIN_MOSI * 4)); c |= (1u << (PIN_MOSI * 4));   // PC0 out
    c &= ~(0xFu << (PIN_MISO * 4)); c |= (0u << (PIN_MISO * 4));   // PC1 in
    c &= ~(0xFu << (PIN_SCLK * 4)); c |= (1u << (PIN_SCLK * 4));   // PC2 out
    C_CFG0 = c;

    uint32_t a0 = A_CFG0;
    a0 &= ~(0xFu << (PIN_IRQ * 4)); a0 |= (0u << (PIN_IRQ * 4));   // PA2 in
    A_CFG0 = a0;

    uint32_t a2 = A_CFG2;
    int cs_bit = (PIN_CS - 20) * 4;                               // PA21 -> CFG2 bit 4
    a2 &= ~(0xFu << cs_bit); a2 |= (1u << cs_bit);                 // PA21 out
    A_CFG2 = a2;

    cs_high();
    sclk_low();
}

// bit-bang SPI, MSB first, mode 0 (CPOL=0, CPHA=0)
static uint16_t spi_xfer16(uint16_t out) {
    uint16_t in = 0;
    for (int i = 15; i >= 0; i--) {
        if (out & (1 << i)) mosi_high(); else mosi_low();
        sclk_high();
        in = (in << 1) | miso_read();
        sclk_low();
    }
    return in;
}

// Чтение канала XPT2046: 8 бит команды + 12 бит результата
//   X: 0x90, Y: 0xD0 (12-bit, differential reference)
static uint16_t read_channel(uint8_t cmd) {
    uint16_t v = 0;
    cs_low();
    for (int i = 7; i >= 0; i--) {
        if (cmd & (1 << i)) mosi_high(); else mosi_low();
        sclk_high(); sclk_low();
    }
    sclk_high(); sclk_low();   // dummy (busy)
    for (int i = 0; i < 12; i++) {
        sclk_high();
        v = (v << 1) | miso_read();
        sclk_low();
    }
    cs_high();
    return v;
}

void touch_init(void) {
    port_pins_cfg();
}

int touch_read(int* x, int* y) {
    // IRQ низкий = панель зафиксировала касание (PENIRQ)
    if (irq_read() != 0)
        return 0;

    uint16_t raw_x = read_channel(0x90);
    uint16_t raw_y = read_channel(0xD0);
    *x = raw_x;
    *y = raw_y;
    return 1;
}