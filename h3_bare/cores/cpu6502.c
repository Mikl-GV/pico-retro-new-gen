#include "cpu6502.h"

static uint8_t  rd(emu6502_t* e, uint16_t a) { return e->read(e->mem_ctx, a); }
static void     wr(emu6502_t* e, uint16_t a, uint8_t v) { e->write(e->mem_ctx, a, v); }
static uint8_t  pop(emu6502_t* e) { return rd(e, 0x0100 + (++e->cpu.sp)); }
static void     push(emu6502_t* e, uint8_t v) { wr(e, 0x0100 + (e->cpu.sp--), v); }
static uint16_t rd16(emu6502_t* e, uint16_t a) { return rd(e, a) | (rd(e, a + 1) << 8); }

#define IMM ((uint8_t)rd(e, e->cpu.pc++))

static uint16_t zp0(emu6502_t* e, uint16_t* xp) { *xp = 0; return rd(e, e->cpu.pc++); }
static uint16_t zpx(emu6502_t* e, uint16_t* xp) { *xp = 0; return (rd(e, e->cpu.pc++) + e->cpu.x) & 0xFF; }
static uint16_t zpy(emu6502_t* e, uint16_t* xp) { *xp = 0; return (rd(e, e->cpu.pc++) + e->cpu.y) & 0xFF; }
static uint16_t abs_(emu6502_t* e, uint16_t* xp) { *xp = 0; uint16_t a = rd(e, e->cpu.pc) | (rd(e, e->cpu.pc + 1) << 8); e->cpu.pc += 2; return a; }
static uint16_t absx(emu6502_t* e, uint16_t* xp) { uint16_t a = rd(e, e->cpu.pc) | (rd(e, e->cpu.pc + 1) << 8); e->cpu.pc += 2; *xp = ((a & 0xFF) + e->cpu.x) > 0xFF ? 1 : 0; return a + e->cpu.x; }
static uint16_t absy(emu6502_t* e, uint16_t* xp) { uint16_t a = rd(e, e->cpu.pc) | (rd(e, e->cpu.pc + 1) << 8); e->cpu.pc += 2; *xp = ((a & 0xFF) + e->cpu.y) > 0xFF ? 1 : 0; return a + e->cpu.y; }
static uint16_t ind(emu6502_t* e, uint16_t* xp)  { *xp = 0; uint16_t a = rd(e, e->cpu.pc) | (rd(e, e->cpu.pc + 1) << 8); e->cpu.pc += 2; uint16_t l = rd(e, a); uint16_t h = rd(e, (a & 0xFF) == 0xFF ? (a & 0xFF00) : (a + 1)); return l | (h << 8); }
static uint16_t indx(emu6502_t* e, uint16_t* xp) { *xp = 0; uint8_t z = (rd(e, e->cpu.pc++) + e->cpu.x) & 0xFF; return rd(e, z) | (rd(e, (z + 1) & 0xFF) << 8); }
static uint16_t indy(emu6502_t* e, uint16_t* xp) { uint8_t z = rd(e, e->cpu.pc++); uint16_t a = rd(e, z) | (rd(e, (z + 1) & 0xFF) << 8); *xp = ((a & 0xFF) + e->cpu.y) > 0xFF ? 1 : 0; return a + e->cpu.y; }
static uint16_t rel(emu6502_t* e, uint16_t* xp)  { *xp = 0; return rd(e, e->cpu.pc++); }

#define FLAG_N 0x80
#define FLAG_V 0x40
#define FLAG_U 0x20
#define FLAG_B 0x10
#define FLAG_D 0x08
#define FLAG_I 0x04
#define FLAG_Z 0x02
#define FLAG_C 0x01

static void set_nz(emu6502_t* e, uint8_t r) {
    e->cpu.p &= ~(FLAG_N | FLAG_Z);
    if (r & 0x80) e->cpu.p |= FLAG_N;
    if (r == 0)   e->cpu.p |= FLAG_Z;
}

// ADC: A + M + C, флаги N V Z C
static void adc_in(emu6502_t* e, uint8_t v) {
    volatile uint16_t sum = (uint16_t)e->cpu.a + v + (e->cpu.p & FLAG_C);
    volatile uint8_t r = (uint8_t)sum;
    e->cpu.p &= ~(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);
    if (r & 0x80)                   e->cpu.p |= FLAG_N;
    if (r == 0)                     e->cpu.p |= FLAG_Z;
    if (sum & 0x100)                e->cpu.p |= FLAG_C;
    if (((e->cpu.a ^ r) & (v ^ r)) & 0x80) e->cpu.p |= FLAG_V;
    e->cpu.a = r;
}

// SBC: A - M - (1-C), флаги N V Z C
// SBC: A - M - (1-C). Через беззнаковое сложение A + ~M + C (эквивалент,
// без int-promotion и знаковых ветвлений на ARM).
static void sbc_in(emu6502_t* e, uint8_t v) {
    volatile uint16_t a = e->cpu.a;
    volatile uint16_t m = (uint16_t)v ^ 0xFFu;      // ~M в 16 битах
    volatile uint16_t c = (e->cpu.p & FLAG_C) ? 1u : 0u;
    volatile uint16_t result = a + m + c;
    volatile uint8_t r = (uint8_t)result;

    e->cpu.p &= ~(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);
    if (r & 0x80)                        e->cpu.p |= FLAG_N;
    if (r == 0)                          e->cpu.p |= FLAG_Z;
    if (result > 0xFF)                   e->cpu.p |= FLAG_C;   // переноса не было -> C=1
    if (((e->cpu.a ^ r) & ((uint8_t)~v ^ r)) & 0x80) e->cpu.p |= FLAG_V;
    e->cpu.a = r;
}

void cpu6502_init(emu6502_t* e) {
    e->cpu.pc = rd16(e, 0xFFFC);
    e->cpu.sp = 0xFD;
    e->cpu.a = e->cpu.x = e->cpu.y = 0;
    e->cpu.p = FLAG_U | FLAG_I;
    e->cpu.cycles = 0;
}

// Таблица циклов 6502 (каноническая, без page-пенальти и taken-веток)
static const uint8_t op_cycles[256] = {
//  0 1 2 3 4 5 6 7 8 9 A B C D E F
    7,6,2,2,3,3,5,5,3,2,2,2,2,4,6,6, // 0x00
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0x10
    6,6,2,2,3,3,5,5,4,2,2,2,4,4,6,6, // 0x20
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0x30
    6,6,2,2,3,3,5,5,3,2,2,2,3,4,6,6, // 0x40
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0x50
    6,6,2,2,3,3,5,5,4,2,2,2,5,4,6,6, // 0x60
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0x70
    2,6,2,2,3,3,5,5,3,2,2,2,4,4,6,6, // 0x80
    2,6,2,2,4,4,6,6,2,5,2,2,4,4,7,7, // 0x90
    2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,6, // 0xA0
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0xB0
    2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,6, // 0xC0
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7, // 0xD0
    2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,6, // 0xE0
    2,5,2,2,4,4,6,6,2,4,2,2,4,4,7,7  // 0xF0
};

void cpu6502_exec(emu6502_t* e, uint32_t max_cycles) {
    while (e->cpu.cycles < max_cycles) {
        uint8_t op = rd(e, e->cpu.pc++);
        uint16_t xp, addr;
        uint8_t val;

        switch (op) {
        // ADC
        case 0x69: adc_in(e, IMM); break;
        case 0x65: addr = zp0(e, &xp); adc_in(e, rd(e, addr)); break;
        case 0x75: addr = zpx(e, &xp); adc_in(e, rd(e, addr)); break;
        case 0x6D: addr = abs_(e, &xp); adc_in(e, rd(e, addr)); break;
        case 0x7D: addr = absx(e, &xp); e->cpu.cycles += xp; adc_in(e, rd(e, addr)); break;
        case 0x79: addr = absy(e, &xp); e->cpu.cycles += xp; adc_in(e, rd(e, addr)); break;
        case 0x61: addr = indx(e, &xp); adc_in(e, rd(e, addr)); break;
        case 0x71: addr = indy(e, &xp); e->cpu.cycles += xp; adc_in(e, rd(e, addr)); break;

        // SBC
        case 0xE9: sbc_in(e, IMM); break;
        case 0xE5: addr = zp0(e, &xp); sbc_in(e, rd(e, addr)); break;
        case 0xF5: addr = zpx(e, &xp); sbc_in(e, rd(e, addr)); break;
        case 0xED: addr = abs_(e, &xp); sbc_in(e, rd(e, addr)); break;
        case 0xFD: addr = absx(e, &xp); e->cpu.cycles += xp; sbc_in(e, rd(e, addr)); break;
        case 0xF9: addr = absy(e, &xp); e->cpu.cycles += xp; sbc_in(e, rd(e, addr)); break;
        case 0xE1: addr = indx(e, &xp); sbc_in(e, rd(e, addr)); break;
        case 0xF1: addr = indy(e, &xp); e->cpu.cycles += xp; sbc_in(e, rd(e, addr)); break;

        // AND
        case 0x29: e->cpu.a &= IMM; set_nz(e, e->cpu.a); break;
        case 0x25: addr = zp0(e, &xp); e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x35: addr = zpx(e, &xp); e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x2D: addr = abs_(e, &xp); e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x3D: addr = absx(e, &xp); e->cpu.cycles += xp; e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x39: addr = absy(e, &xp); e->cpu.cycles += xp; e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x21: addr = indx(e, &xp); e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x31: addr = indy(e, &xp); e->cpu.cycles += xp; e->cpu.a &= rd(e, addr); set_nz(e, e->cpu.a); break;

        // ORA
        case 0x09: e->cpu.a |= IMM; set_nz(e, e->cpu.a); break;
        case 0x05: addr = zp0(e, &xp); e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x15: addr = zpx(e, &xp); e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x0D: addr = abs_(e, &xp); e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x1D: addr = absx(e, &xp); e->cpu.cycles += xp; e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x19: addr = absy(e, &xp); e->cpu.cycles += xp; e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x01: addr = indx(e, &xp); e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x11: addr = indy(e, &xp); e->cpu.cycles += xp; e->cpu.a |= rd(e, addr); set_nz(e, e->cpu.a); break;

        // EOR
        case 0x49: e->cpu.a ^= IMM; set_nz(e, e->cpu.a); break;
        case 0x45: addr = zp0(e, &xp); e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x55: addr = zpx(e, &xp); e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x4D: addr = abs_(e, &xp); e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x5D: addr = absx(e, &xp); e->cpu.cycles += xp; e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x59: addr = absy(e, &xp); e->cpu.cycles += xp; e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x41: addr = indx(e, &xp); e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0x51: addr = indy(e, &xp); e->cpu.cycles += xp; e->cpu.a ^= rd(e, addr); set_nz(e, e->cpu.a); break;

        // LDA
        case 0xA9: e->cpu.a = IMM; set_nz(e, e->cpu.a); break;
        case 0xA5: addr = zp0(e, &xp); e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xB5: addr = zpx(e, &xp); e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xAD: addr = abs_(e, &xp); e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xBD: addr = absx(e, &xp); e->cpu.cycles += xp; e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xB9: addr = absy(e, &xp); e->cpu.cycles += xp; e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xA1: addr = indx(e, &xp); e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;
        case 0xB1: addr = indy(e, &xp); e->cpu.cycles += xp; e->cpu.a = rd(e, addr); set_nz(e, e->cpu.a); break;

        // LDX
        case 0xA2: e->cpu.x = IMM; set_nz(e, e->cpu.x); break;
        case 0xA6: addr = zp0(e, &xp); e->cpu.x = rd(e, addr); set_nz(e, e->cpu.x); break;
        case 0xB6: addr = zpy(e, &xp); e->cpu.x = rd(e, addr); set_nz(e, e->cpu.x); break;
        case 0xAE: addr = abs_(e, &xp); e->cpu.x = rd(e, addr); set_nz(e, e->cpu.x); break;
        case 0xBE: addr = absy(e, &xp); e->cpu.cycles += xp; e->cpu.x = rd(e, addr); set_nz(e, e->cpu.x); break;

        // LDY
        case 0xA0: e->cpu.y = IMM; set_nz(e, e->cpu.y); break;
        case 0xA4: addr = zp0(e, &xp); e->cpu.y = rd(e, addr); set_nz(e, e->cpu.y); break;
        case 0xB4: addr = zpx(e, &xp); e->cpu.y = rd(e, addr); set_nz(e, e->cpu.y); break;
        case 0xAC: addr = abs_(e, &xp); e->cpu.y = rd(e, addr); set_nz(e, e->cpu.y); break;
        case 0xBC: addr = absx(e, &xp); e->cpu.cycles += xp; e->cpu.y = rd(e, addr); set_nz(e, e->cpu.y); break;

        // STA
        case 0x85: addr = zp0(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x95: addr = zpx(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x8D: addr = abs_(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x9D: addr = absx(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x99: addr = absy(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x81: addr = indx(e, &xp); wr(e, addr, e->cpu.a); break;
        case 0x91: addr = indy(e, &xp); wr(e, addr, e->cpu.a); break;

        // STX
        case 0x86: addr = zp0(e, &xp); wr(e, addr, e->cpu.x); break;
        case 0x96: addr = zpy(e, &xp); wr(e, addr, e->cpu.x); break;
        case 0x8E: addr = abs_(e, &xp); wr(e, addr, e->cpu.x); break;

        // STY
        case 0x84: addr = zp0(e, &xp); wr(e, addr, e->cpu.y); break;
        case 0x94: addr = zpx(e, &xp); wr(e, addr, e->cpu.y); break;
        case 0x8C: addr = abs_(e, &xp); wr(e, addr, e->cpu.y); break;

        // ASL
        case 0x0A: e->cpu.p = (e->cpu.p & ~FLAG_C) | ((e->cpu.a >> 7) & FLAG_C); e->cpu.a <<= 1; set_nz(e, e->cpu.a); break;
        case 0x06: addr = zp0(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val <<= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x16: addr = zpx(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val <<= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x0E: addr = abs_(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val <<= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x1E: addr = absx(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val <<= 1; wr(e, addr, val); set_nz(e, val); break;

        // LSR
        case 0x4A: e->cpu.p = (e->cpu.p & ~FLAG_C) | (e->cpu.a & FLAG_C); e->cpu.a >>= 1; set_nz(e, e->cpu.a); break;
        case 0x46: addr = zp0(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val >>= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x56: addr = zpx(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val >>= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x4E: addr = abs_(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val >>= 1; wr(e, addr, val); set_nz(e, val); break;
        case 0x5E: addr = absx(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val >>= 1; wr(e, addr, val); set_nz(e, val); break;

        // ROL
        case 0x2A: { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | ((e->cpu.a >> 7) & FLAG_C); e->cpu.a = (e->cpu.a << 1) | c; set_nz(e, e->cpu.a); break; }
        case 0x26: addr = zp0(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val = (val << 1) | c; wr(e, addr, val); set_nz(e, val); break; }
        case 0x36: addr = zpx(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val = (val << 1) | c; wr(e, addr, val); set_nz(e, val); break; }
        case 0x2E: addr = abs_(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val = (val << 1) | c; wr(e, addr, val); set_nz(e, val); break; }
        case 0x3E: addr = absx(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | ((val >> 7) & FLAG_C); val = (val << 1) | c; wr(e, addr, val); set_nz(e, val); break; }

        // ROR
        case 0x6A: { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | (e->cpu.a & FLAG_C); e->cpu.a = (e->cpu.a >> 1) | (c << 7); set_nz(e, e->cpu.a); break; }
        case 0x66: addr = zp0(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val = (val >> 1) | (c << 7); wr(e, addr, val); set_nz(e, val); break; }
        case 0x76: addr = zpx(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val = (val >> 1) | (c << 7); wr(e, addr, val); set_nz(e, val); break; }
        case 0x6E: addr = abs_(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val = (val >> 1) | (c << 7); wr(e, addr, val); set_nz(e, val); break; }
        case 0x7E: addr = absx(e, &xp); val = rd(e, addr); { uint8_t c = e->cpu.p & FLAG_C; e->cpu.p = (e->cpu.p & ~FLAG_C) | (val & FLAG_C); val = (val >> 1) | (c << 7); wr(e, addr, val); set_nz(e, val); break; }

        // INC / DEC
        case 0xE6: addr = zp0(e, &xp); val = rd(e, addr) + 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xF6: addr = zpx(e, &xp); val = rd(e, addr) + 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xEE: addr = abs_(e, &xp); val = rd(e, addr) + 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xFE: addr = absx(e, &xp); val = rd(e, addr) + 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xC6: addr = zp0(e, &xp); val = rd(e, addr) - 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xD6: addr = zpx(e, &xp); val = rd(e, addr) - 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xCE: addr = abs_(e, &xp); val = rd(e, addr) - 1; wr(e, addr, val); set_nz(e, val); break;
        case 0xDE: addr = absx(e, &xp); val = rd(e, addr) - 1; wr(e, addr, val); set_nz(e, val); break;

        // CMP
        case 0xC9: { uint16_t s = e->cpu.a - IMM; set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xC5: addr = zp0(e, &xp); { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xD5: addr = zpx(e, &xp); { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xCD: addr = abs_(e, &xp); { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xDD: addr = absx(e, &xp); e->cpu.cycles += xp; { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xD9: addr = absy(e, &xp); e->cpu.cycles += xp; { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xC1: addr = indx(e, &xp); { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xD1: addr = indy(e, &xp); e->cpu.cycles += xp; { uint16_t s = e->cpu.a - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }

        // CPX / CPY
        case 0xE0: { uint16_t s = e->cpu.x - IMM; set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xE4: addr = zp0(e, &xp); { uint16_t s = e->cpu.x - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xEC: addr = abs_(e, &xp); { uint16_t s = e->cpu.x - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xC0: { uint16_t s = e->cpu.y - IMM; set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xC4: addr = zp0(e, &xp); { uint16_t s = e->cpu.y - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }
        case 0xCC: addr = abs_(e, &xp); { uint16_t s = e->cpu.y - rd(e, addr); set_nz(e, (uint8_t)s); e->cpu.p = (e->cpu.p & ~FLAG_C) | ((!(s & 0xFF00)) ? FLAG_C : 0); break; }

        // BIT
        case 0x24: addr = zp0(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~(FLAG_N|FLAG_V|FLAG_Z)) | (val & FLAG_N) | (val & FLAG_V) | ((e->cpu.a & val) ? 0 : FLAG_Z); break;
        case 0x2C: addr = abs_(e, &xp); val = rd(e, addr); e->cpu.p = (e->cpu.p & ~(FLAG_N|FLAG_V|FLAG_Z)) | (val & FLAG_N) | (val & FLAG_V) | ((e->cpu.a & val) ? 0 : FLAG_Z); break;

        // JMP / JSR / RTS / RTI / BRK
        case 0x4C: e->cpu.pc = abs_(e, &xp); break;
        case 0x6C: e->cpu.pc = ind(e, &xp); break;
        case 0x20: addr = abs_(e, &xp); push(e, (e->cpu.pc - 1) >> 8); push(e, (e->cpu.pc - 1) & 0xFF); e->cpu.pc = addr; break;
        case 0x60: e->cpu.pc = (pop(e) | (pop(e) << 8)) + 1; break;
        case 0x40: e->cpu.p = pop(e) | FLAG_U; e->cpu.pc = pop(e) | (pop(e) << 8); break;
        case 0x00: e->cpu.pc++; push(e, e->cpu.pc >> 8); push(e, e->cpu.pc & 0xFF); push(e, e->cpu.p | FLAG_B); e->cpu.p |= FLAG_I; e->cpu.pc = rd16(e, 0xFFFE); break;

        // Branches: taken = +1 цикл, page-cross = ещё +1
        case 0x90: { int8_t d = (int8_t)rel(e, &xp); if (!(e->cpu.p & FLAG_C)) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0xB0: { int8_t d = (int8_t)rel(e, &xp); if (e->cpu.p & FLAG_C) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0xF0: { int8_t d = (int8_t)rel(e, &xp); if (e->cpu.p & FLAG_Z) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0xD0: { int8_t d = (int8_t)rel(e, &xp); if (!(e->cpu.p & FLAG_Z)) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0x30: { int8_t d = (int8_t)rel(e, &xp); if (e->cpu.p & FLAG_N) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0x10: { int8_t d = (int8_t)rel(e, &xp); if (!(e->cpu.p & FLAG_N)) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0x70: { int8_t d = (int8_t)rel(e, &xp); if (e->cpu.p & FLAG_V) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }
        case 0x50: { int8_t d = (int8_t)rel(e, &xp); if (!(e->cpu.p & FLAG_V)) { e->cpu.cycles++; uint16_t np = e->cpu.pc + d; if ((e->cpu.pc & 0xFF00) != (np & 0xFF00)) e->cpu.cycles++; e->cpu.pc = np; } break; }

        // Flags
        case 0x18: e->cpu.p &= ~FLAG_C; break;
        case 0x38: e->cpu.p |= FLAG_C; break;
        case 0xD8: e->cpu.p &= ~FLAG_D; break;
        case 0xF8: e->cpu.p |= FLAG_D; break;
        case 0x58: e->cpu.p &= ~FLAG_I; break;
        case 0x78: e->cpu.p |= FLAG_I; break;
        case 0xB8: e->cpu.p &= ~FLAG_V; break;

        // NOP + transfers + stack
        case 0xEA: break;
        case 0xAA: e->cpu.x = e->cpu.a; set_nz(e, e->cpu.x); break;
        case 0x8A: e->cpu.a = e->cpu.x; set_nz(e, e->cpu.a); break;
        case 0xA8: e->cpu.y = e->cpu.a; set_nz(e, e->cpu.y); break;
        case 0x98: e->cpu.a = e->cpu.y; set_nz(e, e->cpu.a); break;
        case 0xBA: e->cpu.x = e->cpu.sp; set_nz(e, e->cpu.x); break;
        case 0x9A: e->cpu.sp = e->cpu.x; break;
        case 0xCA: e->cpu.x--; set_nz(e, e->cpu.x); break;
        case 0xE8: e->cpu.x++; set_nz(e, e->cpu.x); break;
        case 0x88: e->cpu.y--; set_nz(e, e->cpu.y); break;
        case 0xC8: e->cpu.y++; set_nz(e, e->cpu.y); break;
        case 0x48: push(e, e->cpu.a); break;
        case 0x68: e->cpu.a = pop(e); set_nz(e, e->cpu.a); break;
        case 0x08: push(e, e->cpu.p | FLAG_B | FLAG_U); break;
        case 0x28: e->cpu.p = pop(e) | FLAG_U; break;

        default: break; // illegal opcodes — NOP
        }
        e->cpu.cycles += op_cycles[op];
    }
}