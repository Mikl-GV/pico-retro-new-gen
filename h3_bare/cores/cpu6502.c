#include "cpu6502.h"

#define RD(a)   c->read(c->ctx, (a))
#define WR(a,v) c->write(c->ctx, (a), (v))
#define PUSH(v)   { WR(0x100 | c->sp, (v)); c->sp--; }
#define POP()     ( c->sp++, RD(0x100 | c->sp) )
#define SET_NZ(v) c->sr = (c->sr & ~(CPU_N|CPU_Z)) | ((v)&CPU_N) | (((v)==0)?CPU_Z:0)

static inline uint16_t RD16(cpu6502_t* c) {
    uint16_t lo = c->read(c->ctx, c->pc++);
    uint16_t hi = c->read(c->ctx, c->pc++);
    return lo | (hi << 8);
}

#define PAGE_PEN(b,r) (((((b)+(r))^(b))&0x100) ? (--c->cycles) : 0)

// ---------------- арифметика ----------------
static inline void do_adc(cpu6502_t* c, uint8_t m) {
    if (c->sr & CPU_D) {
        uint8_t ao = c->a;
        uint8_t lo = (c->a & 0x0F) + (m & 0x0F) + (c->sr & CPU_C);
        uint8_t hi = (c->a >> 4) + (m >> 4), cin = 0, cout = 0;
        if (lo > 9) { lo += 6; cin = 1; }
        hi += cin;
        if (hi > 9) { hi += 6; cout = 1; }
        uint8_t r = (hi << 4) | (lo & 0x0F);
        c->a = r;
        SET_NZ(c->a);
        c->sr = (c->sr & ~(CPU_V|CPU_C)) | (((~(ao^m))&(ao^r)&0x80)?CPU_V:0) | (cout?CPU_C:0);
        return;
    }
    uint16_t t = (uint16_t)c->a + m + (c->sr & CPU_C);
    uint8_t ao = c->a;
    c->a = (uint8_t)t;
    SET_NZ(c->a);
    c->sr = (c->sr & ~(CPU_V|CPU_C)) | (((~(ao^m))&(ao^c->a)&0x80)?CPU_V:0) | (t>0xFF?CPU_C:0);
}

static inline void do_sbc(cpu6502_t* c, uint8_t m) {
    if (c->sr & CPU_D) {
        uint8_t ao = c->a;
        uint8_t bo = 1 - (c->sr & CPU_C);
        uint8_t lo = (c->a & 0x0F) - (m & 0x0F) - bo;
        uint8_t hi = (c->a >> 4) - (m >> 4), co = 1;
        if (lo & 0x80) { lo -= 6; hi--; }
        if (hi & 0x80) { hi -= 6; co = 0; }
        uint8_t r = (hi << 4) | (lo & 0x0F);
        c->a = r;
        SET_NZ(c->a);
        c->sr = (c->sr & ~(CPU_V|CPU_C)) | (((ao^m)&(ao^r)&0x80)?CPU_V:0) | (co?CPU_C:0);
        return;
    }
    uint16_t t = (uint16_t)c->a - m - (1 - (c->sr & CPU_C));
    uint8_t ao = c->a;
    c->a = (uint8_t)t;
    SET_NZ(c->a);
    c->sr = (c->sr & ~(CPU_V|CPU_C)) | (((ao^m)&(ao^c->a)&0x80)?CPU_V:0) | (t<0x100?CPU_C:0);
}

#define DO_CMP(r_,m_) { uint8_t d = (uint8_t)((r_)-(m_)); \
    c->sr = (c->sr & ~(CPU_N|CPU_Z|CPU_C)) | (d&CPU_N) | ((d==0)?CPU_Z:0) | (((r_)>=(m_))?CPU_C:0); }

void cpu6502_reset(cpu6502_t* c) {
    c->a = c->x = c->y = 0;
    c->sp = 0xFD;
    c->sr = CPU_I | 0x20;
    c->cycles = 0;
    c->pc = (uint16_t)RD(0xFFFC) | ((uint16_t)RD(0xFFFD) << 8);
}

void cpu6502_run(cpu6502_t* c, int32_t cycles) {
    c->cycles = cycles;
    if (c->cycles < 0) return;

    for (;;) {
        if (c->cycles < 0) return;
        uint8_t op = RD(c->pc++);

        switch (op) {
        case 0xEA: c->cycles -= 2; break;
        default:   c->cycles -= 2; break;

        // LDA
        case 0xA9: c->cycles-=2; c->a=RD(c->pc++); SET_NZ(c->a); break;
        case 0xA5: c->cycles-=3; c->a=RD(RD(c->pc++)); SET_NZ(c->a); break;
        case 0xB5: c->cycles-=4; c->a=RD((uint8_t)(RD(c->pc++)+c->x)); SET_NZ(c->a); break;
        case 0xAD: c->cycles-=4; { uint16_t a=RD16(c); c->a=RD(a); SET_NZ(c->a); } break;
        case 0xBD: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); c->a=RD(b+c->x); SET_NZ(c->a); } break;
        case 0xB9: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); c->a=RD(b+c->y); SET_NZ(c->a); } break;
        case 0xA1: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); c->a=RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8)); SET_NZ(c->a); } break;
        case 0xB1: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); c->a=RD(b+c->y); SET_NZ(c->a); } break;

        // LDX
        case 0xA2: c->cycles-=2; c->x=RD(c->pc++); SET_NZ(c->x); break;
        case 0xA6: c->cycles-=3; c->x=RD(RD(c->pc++)); SET_NZ(c->x); break;
        case 0xB6: c->cycles-=4; c->x=RD((uint8_t)(RD(c->pc++)+c->y)); SET_NZ(c->x); break;
        case 0xAE: c->cycles-=4; { uint16_t a=RD16(c); c->x=RD(a); SET_NZ(c->x); } break;
        case 0xBE: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); c->x=RD(b+c->y); SET_NZ(c->x); } break;

        // LDY
        case 0xA0: c->cycles-=2; c->y=RD(c->pc++); SET_NZ(c->y); break;
        case 0xA4: c->cycles-=3; c->y=RD(RD(c->pc++)); SET_NZ(c->y); break;
        case 0xB4: c->cycles-=4; c->y=RD((uint8_t)(RD(c->pc++)+c->x)); SET_NZ(c->y); break;
        case 0xAC: c->cycles-=4; { uint16_t a=RD16(c); c->y=RD(a); SET_NZ(c->y); } break;
        case 0xBC: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); c->y=RD(b+c->x); SET_NZ(c->y); } break;

        // STA
        case 0x85: c->cycles-=3; WR(RD(c->pc++), c->a); break;
        case 0x95: c->cycles-=4; WR((uint8_t)(RD(c->pc++)+c->x), c->a); break;
        case 0x8D: c->cycles-=4; WR(RD16(c), c->a); break;
        case 0x9D: c->cycles-=5; { uint16_t b=RD16(c); WR(b+c->x, c->a); } break;
        case 0x99: c->cycles-=5; { uint16_t b=RD16(c); WR(b+c->y, c->a); } break;
        case 0x81: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); WR(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8), c->a); } break;
        case 0x91: c->cycles-=6; { uint8_t z=RD(c->pc++); WR((RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8))+c->y, c->a); } break;

        // STX / STY
        case 0x86: c->cycles-=3; WR(RD(c->pc++), c->x); break;
        case 0x96: c->cycles-=4; WR((uint8_t)(RD(c->pc++)+c->y), c->x); break;
        case 0x8E: c->cycles-=4; WR(RD16(c), c->x); break;
        case 0x84: c->cycles-=3; WR(RD(c->pc++), c->y); break;
        case 0x94: c->cycles-=4; WR((uint8_t)(RD(c->pc++)+c->x), c->y); break;
        case 0x8C: c->cycles-=4; WR(RD16(c), c->y); break;

        // AND/ORA/EOR
        case 0x29: c->cycles-=2; c->a&=RD(c->pc++); SET_NZ(c->a); break;
        case 0x25: c->cycles-=3; c->a&=RD(RD(c->pc++)); SET_NZ(c->a); break;
        case 0x35: c->cycles-=4; c->a&=RD((uint8_t)(RD(c->pc++)+c->x)); SET_NZ(c->a); break;
        case 0x2D: c->cycles-=4; { uint16_t a=RD16(c); c->a&=RD(a); SET_NZ(c->a); } break;
        case 0x3D: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); c->a&=RD(b+c->x); SET_NZ(c->a); } break;
        case 0x39: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); c->a&=RD(b+c->y); SET_NZ(c->a); } break;
        case 0x21: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); c->a&=RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8)); SET_NZ(c->a); } break;
        case 0x31: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); c->a&=RD(b+c->y); SET_NZ(c->a); } break;

        case 0x09: c->cycles-=2; c->a|=RD(c->pc++); SET_NZ(c->a); break;
        case 0x05: c->cycles-=3; c->a|=RD(RD(c->pc++)); SET_NZ(c->a); break;
        case 0x15: c->cycles-=4; c->a|=RD((uint8_t)(RD(c->pc++)+c->x)); SET_NZ(c->a); break;
        case 0x0D: c->cycles-=4; { uint16_t a=RD16(c); c->a|=RD(a); SET_NZ(c->a); } break;
        case 0x1D: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); c->a|=RD(b+c->x); SET_NZ(c->a); } break;
        case 0x19: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); c->a|=RD(b+c->y); SET_NZ(c->a); } break;
        case 0x01: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); c->a|=RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8)); SET_NZ(c->a); } break;
        case 0x11: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); c->a|=RD(b+c->y); SET_NZ(c->a); } break;

        case 0x49: c->cycles-=2; c->a^=RD(c->pc++); SET_NZ(c->a); break;
        case 0x45: c->cycles-=3; c->a^=RD(RD(c->pc++)); SET_NZ(c->a); break;
        case 0x55: c->cycles-=4; c->a^=RD((uint8_t)(RD(c->pc++)+c->x)); SET_NZ(c->a); break;
        case 0x4D: c->cycles-=4; { uint16_t a=RD16(c); c->a^=RD(a); SET_NZ(c->a); } break;
        case 0x5D: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); c->a^=RD(b+c->x); SET_NZ(c->a); } break;
        case 0x59: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); c->a^=RD(b+c->y); SET_NZ(c->a); } break;
        case 0x41: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); c->a^=RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8)); SET_NZ(c->a); } break;
        case 0x51: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); c->a^=RD(b+c->y); SET_NZ(c->a); } break;

        // CMP/CPX/CPY
        case 0xC9: c->cycles-=2; DO_CMP(c->a, RD(c->pc++)); break;
        case 0xC5: c->cycles-=3; DO_CMP(c->a, RD(RD(c->pc++))); break;
        case 0xD5: c->cycles-=4; DO_CMP(c->a, RD((uint8_t)(RD(c->pc++)+c->x))); break;
        case 0xCD: c->cycles-=4; { uint16_t a=RD16(c); DO_CMP(c->a, RD(a)); } break;
        case 0xDD: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); DO_CMP(c->a, RD(b+c->x)); } break;
        case 0xD9: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); DO_CMP(c->a, RD(b+c->y)); } break;
        case 0xC1: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); DO_CMP(c->a, RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8))); } break;
        case 0xD1: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); DO_CMP(c->a, RD(b+c->y)); } break;

        case 0xE0: c->cycles-=2; DO_CMP(c->x, RD(c->pc++)); break;
        case 0xE4: c->cycles-=3; DO_CMP(c->x, RD(RD(c->pc++))); break;
        case 0xEC: c->cycles-=4; { uint16_t a=RD16(c); DO_CMP(c->x, RD(a)); } break;
        case 0xC0: c->cycles-=2; DO_CMP(c->y, RD(c->pc++)); break;
        case 0xC4: c->cycles-=3; DO_CMP(c->y, RD(RD(c->pc++))); break;
        case 0xCC: c->cycles-=4; { uint16_t a=RD16(c); DO_CMP(c->y, RD(a)); } break;

        // ADC/SBC
        case 0x69: c->cycles-=2; do_adc(c, RD(c->pc++)); break;
        case 0x65: c->cycles-=3; do_adc(c, RD(RD(c->pc++))); break;
        case 0x75: c->cycles-=4; do_adc(c, RD((uint8_t)(RD(c->pc++)+c->x))); break;
        case 0x6D: c->cycles-=4; { uint16_t a=RD16(c); do_adc(c, RD(a)); } break;
        case 0x7D: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); do_adc(c, RD(b+c->x)); } break;
        case 0x79: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); do_adc(c, RD(b+c->y)); } break;
        case 0x61: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); do_adc(c, RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8))); } break;
        case 0x71: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); do_adc(c, RD(b+c->y)); } break;

        case 0xE9: c->cycles-=2; do_sbc(c, RD(c->pc++)); break;
        case 0xE5: c->cycles-=3; do_sbc(c, RD(RD(c->pc++))); break;
        case 0xF5: c->cycles-=4; do_sbc(c, RD((uint8_t)(RD(c->pc++)+c->x))); break;
        case 0xED: c->cycles-=4; { uint16_t a=RD16(c); do_sbc(c, RD(a)); } break;
        case 0xFD: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->x); do_sbc(c, RD(b+c->x)); } break;
        case 0xF9: c->cycles-=4; { uint16_t b=RD16(c); PAGE_PEN(b,c->y); do_sbc(c, RD(b+c->y)); } break;
        case 0xE1: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); do_sbc(c, RD(RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8))); } break;
        case 0xF1: c->cycles-=5; { uint8_t z=RD(c->pc++); uint16_t b=RD(z)|((uint16_t)RD((uint8_t)(z+1))<<8); PAGE_PEN(b,c->y); do_sbc(c, RD(b+c->y)); } break;

        // INC/DEC
        case 0xE6: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z)+1; SET_NZ(v); WR(z,v); } break;
        case 0xF6: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z)+1; SET_NZ(v); WR(z,v); } break;
        case 0xEE: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a)+1; SET_NZ(v); WR(a,v); } break;
        case 0xFE: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a)+1; SET_NZ(v); WR(a,v); } break;
        case 0xC6: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z)-1; SET_NZ(v); WR(z,v); } break;
        case 0xD6: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z)-1; SET_NZ(v); WR(z,v); } break;
        case 0xCE: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a)-1; SET_NZ(v); WR(a,v); } break;
        case 0xDE: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a)-1; SET_NZ(v); WR(a,v); } break;

        // ASL/LSR/ROL/ROR memory
        case 0x06: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z); c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v<<=1; SET_NZ(v); WR(z,v); } break;
        case 0x16: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z); c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v<<=1; SET_NZ(v); WR(z,v); } break;
        case 0x0E: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a); c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v<<=1; SET_NZ(v); WR(a,v); } break;
        case 0x1E: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a); c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v<<=1; SET_NZ(v); WR(a,v); } break;
        case 0x46: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z); c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v>>=1; SET_NZ(v); WR(z,v); } break;
        case 0x56: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z); c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v>>=1; SET_NZ(v); WR(z,v); } break;
        case 0x4E: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a); c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v>>=1; SET_NZ(v); WR(a,v); } break;
        case 0x5E: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a); c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v>>=1; SET_NZ(v); WR(a,v); } break;
        case 0x26: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z); uint8_t oc=(c->sr&CPU_C)?1:0; c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v=(uint8_t)((v<<1)|oc); SET_NZ(v); WR(z,v); } break;
        case 0x36: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z); uint8_t oc=(c->sr&CPU_C)?1:0; c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v=(uint8_t)((v<<1)|oc); SET_NZ(v); WR(z,v); } break;
        case 0x2E: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a); uint8_t oc=(c->sr&CPU_C)?1:0; c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v=(uint8_t)((v<<1)|oc); SET_NZ(v); WR(a,v); } break;
        case 0x3E: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a); uint8_t oc=(c->sr&CPU_C)?1:0; c->sr=(c->sr&~CPU_C)|((v&0x80)?CPU_C:0); v=(uint8_t)((v<<1)|oc); SET_NZ(v); WR(a,v); } break;
        case 0x66: c->cycles-=5; { uint8_t z=RD(c->pc++); uint8_t v=RD(z); uint8_t oc=(c->sr&CPU_C)?0x80:0; c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v=(uint8_t)((v>>1)|oc); SET_NZ(v); WR(z,v); } break;
        case 0x76: c->cycles-=6; { uint8_t z=(uint8_t)(RD(c->pc++)+c->x); uint8_t v=RD(z); uint8_t oc=(c->sr&CPU_C)?0x80:0; c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v=(uint8_t)((v>>1)|oc); SET_NZ(v); WR(z,v); } break;
        case 0x6E: c->cycles-=6; { uint16_t a=RD16(c); uint8_t v=RD(a); uint8_t oc=(c->sr&CPU_C)?0x80:0; c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v=(uint8_t)((v>>1)|oc); SET_NZ(v); WR(a,v); } break;
        case 0x7E: c->cycles-=7; { uint16_t b=RD16(c); uint16_t a=b+c->x; uint8_t v=RD(a); uint8_t oc=(c->sr&CPU_C)?0x80:0; c->sr=(c->sr&~CPU_C)|((v&0x01)?CPU_C:0); v=(uint8_t)((v>>1)|oc); SET_NZ(v); WR(a,v); } break;

        // ASL/LSR/ROL/ROR accumulator
        case 0x0A: c->cycles-=2; c->sr=(c->sr&~CPU_C)|((c->a&0x80)?CPU_C:0); c->a<<=1; SET_NZ(c->a); break;
        case 0x4A: c->cycles-=2; c->sr=(c->sr&~CPU_C)|((c->a&0x01)?CPU_C:0); c->a>>=1; SET_NZ(c->a); break;
        case 0x2A: c->cycles-=2; { uint8_t oc=(c->sr&CPU_C)?1:0; c->sr=(c->sr&~CPU_C)|((c->a&0x80)?CPU_C:0); c->a=(uint8_t)((c->a<<1)|oc); SET_NZ(c->a); } break;
        case 0x6A: c->cycles-=2; { uint8_t oc=(c->sr&CPU_C)?0x80:0; c->sr=(c->sr&~CPU_C)|((c->a&0x01)?CPU_C:0); c->a=(uint8_t)((c->a>>1)|oc); SET_NZ(c->a); } break;

        // BIT
        case 0x24: c->cycles-=3; { uint8_t m=RD(RD(c->pc++)); c->sr=(c->sr&~(CPU_N|CPU_V|CPU_Z))|(m&(CPU_N|CPU_V))|((c->a&m)?0:CPU_Z); } break;
        case 0x2C: c->cycles-=4; { uint16_t a=RD16(c); uint8_t m=RD(a); c->sr=(c->sr&~(CPU_N|CPU_V|CPU_Z))|(m&(CPU_N|CPU_V))|((c->a&m)?0:CPU_Z); } break;

        // Branches
        #define BR(cond) { int8_t rel=(int8_t)RD(c->pc); c->pc++; uint16_t np=c->pc; \
            if(cond){ uint16_t d=np+rel; c->cycles-=((d^np)&0x0100)?4:3; c->pc=d; } \
            else c->cycles-=2; } break

        case 0x10: BR(!(c->sr&CPU_N));
        case 0x30: BR(c->sr&CPU_N);
        case 0x50: BR(!(c->sr&CPU_V));
        case 0x70: BR(c->sr&CPU_V);
        case 0x90: BR(!(c->sr&CPU_C));
        case 0xB0: BR(c->sr&CPU_C);
        case 0xD0: BR(!(c->sr&CPU_Z));
        case 0xF0: BR(c->sr&CPU_Z);

        // Регистры/стек/флаги
        case 0xAA: c->cycles-=2; c->x=c->a; SET_NZ(c->x); break;
        case 0xA8: c->cycles-=2; c->y=c->a; SET_NZ(c->y); break;
        case 0x8A: c->cycles-=2; c->a=c->x; SET_NZ(c->a); break;
        case 0x98: c->cycles-=2; c->a=c->y; SET_NZ(c->a); break;
        case 0xBA: c->cycles-=2; c->x=c->sp; SET_NZ(c->x); break;
        case 0x9A: c->cycles-=2; c->sp=c->x; break;
        case 0x48: c->cycles-=3; PUSH(c->a); break;
        case 0x68: c->cycles-=4; c->a=POP(); SET_NZ(c->a); break;
        case 0x08: c->cycles-=3; PUSH(c->sr|CPU_B); break;
        case 0x28: c->cycles-=4; c->sr=POP(); break;
        case 0x18: c->cycles-=2; c->sr&=~CPU_C; break;
        case 0x38: c->cycles-=2; c->sr|=CPU_C; break;
        case 0x58: c->cycles-=2; c->sr&=~CPU_I; break;
        case 0x78: c->cycles-=2; c->sr|=CPU_I; break;
        case 0xB8: c->cycles-=2; c->sr&=~CPU_V; break;
        case 0xD8: c->cycles-=2; c->sr&=~CPU_D; break;
        case 0xF8: c->cycles-=2; c->sr|=CPU_D; break;
        case 0xE8: c->cycles-=2; c->x++; SET_NZ(c->x); break;
        case 0xC8: c->cycles-=2; c->y++; SET_NZ(c->y); break;
        case 0xCA: c->cycles-=2; c->x--; SET_NZ(c->x); break;
        case 0x88: c->cycles-=2; c->y--; SET_NZ(c->y); break;

        // Переходы
        case 0x4C: c->cycles-=3; c->pc=RD16(c); break;
        case 0x6C: c->cycles-=5; { uint16_t a=RD16(c); uint16_t lo=RD(a); uint16_t hi=RD((a&0xFF00)|((uint8_t)(a+1))); c->pc=lo|(hi<<8); } break;
        case 0x20: c->cycles-=6; { uint16_t a=RD16(c); uint16_t ret=c->pc-1; PUSH(ret>>8); PUSH(ret&0xFF); c->pc=a; } break;
        case 0x60: c->cycles-=6; { uint8_t lo=POP(); uint8_t hi=POP(); c->pc=(uint16_t)((lo|((uint16_t)hi<<8))+1); } break;
        case 0x40: c->cycles-=6; c->sr=POP(); { uint8_t lo=POP(); uint8_t hi=POP(); c->pc=lo|((uint16_t)hi<<8); } break;
        case 0x00: c->cycles-=7; c->pc++; PUSH(c->pc>>8); PUSH(c->pc&0xFF); PUSH(c->sr|CPU_B); c->sr|=CPU_I; c->pc=RD16(c); break;
        }
    }
}