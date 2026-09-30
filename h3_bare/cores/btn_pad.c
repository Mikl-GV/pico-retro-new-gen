// btn_pad.c — прямой 8-битовый PCF8574@0x20 (TWI0, bit-bang PA11/PA12).
// ОТДЕЛЬНЫЙ модуль: NIKAK не лезем в sega_pad.c (его скан трогать нельзя).
// Карта кнопок (активный низ): B0=Up B1=Left B2=Right B3=Down B4=A B5=B
// B6=Start B7=Select(Coin). Возвращает маску как sega_pad_scan.
#include <stdint.h>
#include "btn_pad.h"
#include "sega_pad.h"
#include "h3.h"
#include "h3_hs_timer.h"

// ---- PA11 = SCL, PA12 = SDA (GPIO на TWI0) ----
#define SCL_PIN  11
#define SDA_PIN  12

#define PA_BASE  0x01C20800u
#define PA_CFG1  (*(volatile uint32_t*)(PA_BASE + 0x04u))
#define PA_DAT   (*(volatile uint32_t*)(PA_BASE + 0x10u))

static inline void bpa_scl_out(int v) {
    uint32_t cfg = PA_CFG1;
    int shift = (SCL_PIN % 8) * 4;
    if (v) { PA_DAT |=  (1u << SCL_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    else   { PA_DAT &= ~(1u << SCL_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    PA_CFG1 = cfg;
}
static inline void bpa_sda_out(int v) {
    uint32_t cfg = PA_CFG1;
    int shift = (SDA_PIN % 8) * 4;
    if (v) { PA_DAT |=  (1u << SDA_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    else   { PA_DAT &= ~(1u << SDA_PIN); cfg = (cfg & ~(0xFu << shift)) | (0x1u << shift); }
    PA_CFG1 = cfg;
}
static inline void bpa_sda_in(void) {
    uint32_t cfg = PA_CFG1;
    cfg &= ~(0xFu << ((SDA_PIN % 8) * 4));
    PA_CFG1 = cfg;
}
static inline int bpa_sda_read(void) { return (PA_DAT & (1u << SDA_PIN)) ? 1 : 0; }

static inline void bpa_half(void) {
    const uint32_t t0 = H3_HS_TIMER->CURNT_LO;
    while ((t0 - H3_HS_TIMER->CURNT_LO) < 130) { __asm__ volatile("nop"); }
}
static void bpa_start(void) { bpa_sda_out(1); bpa_scl_out(1); bpa_half();
                              bpa_sda_out(0); bpa_half(); bpa_scl_out(0); bpa_half(); }
static void bpa_stop(void)  { bpa_sda_out(0); bpa_scl_out(1); bpa_half();
                              bpa_sda_out(1); bpa_half(); }
static int bpa_write(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        bpa_sda_out((b >> i) & 1); bpa_half();
        bpa_scl_out(1); bpa_half(); bpa_scl_out(0); bpa_half();
    }
    bpa_sda_in(); bpa_half();
    bpa_scl_out(1); bpa_half();
    int ack = bpa_sda_read();
    bpa_scl_out(0); bpa_half();
    return ack;
}
static uint8_t bpa_read(int last) {
    uint8_t b = 0;
    bpa_sda_in();
    for (int i = 7; i >= 0; i--) {
        bpa_scl_out(1); bpa_half();
        if (bpa_sda_read()) b |= (1u << i);
        bpa_scl_out(0); bpa_half();
    }
    bpa_sda_out(last ? 1 : 0); bpa_half();
    bpa_scl_out(1); bpa_half(); bpa_scl_out(0); bpa_half();
    bpa_sda_in();
    return b;
}
// Чтение одного байта из PCF8574 (адрес 0x20 read = 0x41).
static int btn_pad_read(uint8_t* out) {
    bpa_start();
    if (bpa_write(0x41)) { bpa_stop(); return 0; }
    *out = bpa_read(1);
    bpa_stop();
    return 1;
}

uint16_t btn_pad8_scan(void) {
    uint8_t r;
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