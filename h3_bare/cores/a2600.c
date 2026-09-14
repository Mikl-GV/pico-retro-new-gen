// a2600.c — Atari 2600 (TIA + RIOT) с нуля.
// Модель: за строку выполняем 76 "виртуальных" циклов CPU (приближение),
// TIA-записи применяются сразу, RESP-позиции засекаются по циклу внутри строки.
// Далее: рендер строки (playfield + 2 игрока + 2 ракетки + мяч), латчи столкновений.
#include <string.h>
#include "a2600.h"
#include "uart.h"

#define LINE_CYCLES  76
#define VIS_LINES    192
#define TOTAL_LINES  262
#define VBLANK_START 3        // строки VSYNC: 0..2
#define VIS_START    40       // первая видимая строка
#define HBLANK       68       // цвет-тактов до видимой области (приближение)

// ---------- приближённая NTSC-палитра 2600 (128 цветов) ----------
static uint16_t ntsc_pal[128];
static int pal_init = 0;

static void init_palette(void) {
    for (int i = 0; i < 128; i++) {
        int hue = i >> 4;      // 0..7
        int lum = i & 0xF;     // 0..15
        if (lum == 0) { ntsc_pal[i] = 0; continue; }
        double h = 360.0 * hue / 8.0 + 250.0;
        double s = 0.75, v = lum / 15.0;
        double r, g, b;
        int hi = (int)(h / 60.0) % 6;
        double f = h / 60.0 - (int)(h / 60.0);
        double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (hi) {
            case 0: r=v; g=t; b=p; break;
            case 1: r=q; g=v; b=p; break;
            case 2: r=p; g=v; b=t; break;
            case 3: r=p; g=q; b=v; break;
            case 4: r=t; g=p; b=v; break;
            default:r=v; g=p; b=q; break;
        }
        uint16_t rr = (uint16_t)(r * 31), gg = (uint16_t)(g * 63), bb = (uint16_t)(b * 31);
        ntsc_pal[i] = (uint16_t)((rr << 11) | (gg << 5) | bb);
    }
}

// ---------- TIA-адреса (write) ----------
#define VBLANK  0x01
#define WSYNC   0x02
#define NUSIZ0  0x04
#define NUSIZ1  0x05
#define COLUP0  0x06
#define COLUP1  0x07
#define COLUM0  0x08
#define COLUM1  0x09
#define COLUBK  0x0A
#define COLUPF  0x0B
#define CTRLPF  0x0C
#define REFP0   0x0D
#define REFP1   0x0E
#define PF0     0x0F
#define PF1     0x10
#define PF2     0x11
#define RESP0   0x12
#define RESP1   0x13
#define RESM0   0x14
#define RESM1   0x15
#define RESBL   0x16
#define AUDC0   0x17
#define AUDC1   0x18
#define AUDF0   0x19
#define AUDF1   0x1A
#define AUDV0   0x1B
#define AUDV1   0x1C
#define GRP0    0x1D
#define GRP1    0x1E
#define ENAM0   0x1F
#define ENAM1   0x20
#define ENABL   0x21
#define HMP0    0x22
#define HMP1    0x23
#define HMM0    0x24
#define HMM1    0x25
#define HMBL    0x26
#define VDELP0  0x27
#define VDELP1  0x28
#define RESMP0  0x29
#define RESMP1  0x2A
#define HMOVE   0x2B
#define HMCLR   0x2C
#define CXCLR   0x2D

// RIOT порты
#define SWCHA   0x280
#define SWCHB   0x282
#define INTIM   0x284
#define TIM1T   0x294
#define TIM8T   0x295
#define TIM64T  0x296
#define T1024T  0x297

// ---------- internal TIA state ----------
// маски объектов в colvect
#define PL0_MASK 0x01
#define PL1_MASK 0x02
#define ML0_MASK 0x04
#define ML1_MASK 0x08
#define BL_MASK  0x10
#define PF_MASK  0x20

static uint8_t t_vsync, t_vblank;
static uint8_t t_pf0, t_pf1, t_pf2, t_ctrlpf;
static uint8_t t_colup0, t_colup1, t_colum0, t_colum1, t_colubk, t_colupf;
static uint8_t t_grp0, t_grp1, t_grp0_prev, t_grp1_prev;
static uint8_t t_enam0, t_enam1, t_enabl;
static uint8_t t_nusiz0, t_nusiz1;
static uint8_t t_refp0, t_refp1;
static uint8_t t_vdelp0, t_vdelp1;
static uint8_t t_resmp0, t_resmp1;
static uint8_t t_hm0, t_hm1, t_hmm0, t_hmm1, t_hmbl;
// объекты (позиция луча в момент RESP)
static int t_p0x, t_p1x, t_m0x, t_m1x, t_blx;
// collision latches (TIA-формат: биты D7 и D6)
static uint8_t cx_m0p, cx_m1p, cx_p0fb, cx_p1fb, cx_m0fb, cx_m1fb, cx_blpf, cx_ppmm;
// RIOT
static uint8_t riot_ram[128];
static uint32_t timer_set_cc;  // цветовые такты в момент установки таймера
static uint32_t timer_set_count;
static int timer_res;
static uint32_t g_cc;           // монотонный счётчик цветовых тактов (1 CPU цикл = 3 CC)

static uint8_t joy0 = 0xFF;
static uint8_t fire0 = 1;
static uint8_t swchb = 0x7F;
static uint32_t frame_index = 0;
static uint8_t wsync_flag = 0;

// beam_x в пикселях строки (0..159), или <0 в HBLANK
static int beam_x(void) {
    return (int)((g_cc % 228u) - 68);
}
// номер строки (0..261)
static int beam_line(void) {
    return (int)(g_cc / 228u);
}

static void hmove_obj(int* x, uint8_t hmm) {
    if (hmm & 0x08)
        *x += ((hmm ^ 0x0F) + 1);
    else
        *x -= hmm;
    if (*x > 160)       *x = -68;
    else if (*x < -68)  *x = 160;
}

static void tia_write(uint8_t reg, uint8_t v) {
    switch (reg) {
        case 0x00: t_vsync = v; break;
        case VBLANK: t_vblank = v; break;
        case WSYNC: wsync_flag = 1; break;
        case NUSIZ0: t_nusiz0 = v; break;
        case NUSIZ1: t_nusiz1 = v; break;
        case COLUP0: t_colup0 = v; break;
        case COLUP1: t_colup1 = v; break;
        case COLUM0: t_colum0 = v; break;
        case COLUM1: t_colum1 = v; break;
        case COLUBK: t_colubk = v; break;
        case COLUPF: t_colupf = v; break;
        case CTRLPF: t_ctrlpf = v & 0x37; break;
        case REFP0: t_refp0 = (v & 0x08) ? 1 : 0; break;
        case REFP1: t_refp1 = (v & 0x08) ? 1 : 0; break;
        case PF0: t_pf0 = v; break;
        case PF1: t_pf1 = v; break;
        case PF2: t_pf2 = v; break;
        case RESP0: { int bx = beam_x(); t_p0x = bx < 0 ? 0 : bx; break; }
        case RESP1: { int bx = beam_x(); t_p1x = bx < 0 ? 0 : bx; break; }
        case RESM0: { int bx = beam_x(); t_m0x = bx < 0 ? 0 : bx; break; }
        case RESM1: { int bx = beam_x(); t_m1x = bx < 0 ? 0 : bx; break; }
        case RESBL: { int bx = beam_x(); t_blx = bx < 0 ? 0 : bx; break; }
        case AUDC0: case AUDC1: case AUDF0: case AUDF1: case AUDV0: case AUDV1: break;
        case GRP0: t_grp0_prev = t_grp0; t_grp0 = v; break;
        case GRP1: t_grp1_prev = t_grp1; t_grp1 = v; break;
        case ENAM0: t_enam0 = v & 2; if (t_resmp0) t_enam0 = 0; break;
        case ENAM1: t_enam1 = v & 2; if (t_resmp1) t_enam1 = 0; break;
        case ENABL: t_enabl = v & 2; break;
        case HMP0: t_hm0 = v; break;
        case HMP1: t_hm1 = v; break;
        case HMM0: t_hmm0 = v; break;
        case HMM1: t_hmm1 = v; break;
        case HMBL: t_hmbl = v; break;
        case VDELP0: t_vdelp0 = v & 1; break;
        case VDELP1: t_vdelp1 = v & 1; break;
        case RESMP0:
            t_resmp0 = v & 2;
            if (v & 2) { t_m0x = t_p0x + 4; t_enam0 = 0; }
            break;
        case RESMP1:
            t_resmp1 = v & 2;
            if (v & 2) { t_m1x = t_p1x + 4; t_enam1 = 0; }
            break;
        case HMOVE:
            hmove_obj(&t_p0x, (t_hm0 >> 4) & 0xF);
            hmove_obj(&t_p1x, (t_hm1 >> 4) & 0xF);
            hmove_obj(&t_m0x, (t_hmm0 >> 4) & 0xF);
            hmove_obj(&t_m1x, (t_hmm1 >> 4) & 0xF);
            hmove_obj(&t_blx, (t_hmbl >> 4) & 0xF);
            break;
        case HMCLR: t_hm0 = t_hm1 = t_hmm0 = t_hmm1 = t_hmbl = 0; break;
        case CXCLR: cx_m0p = cx_m1p = cx_p0fb = cx_p1fb = cx_m0fb = cx_m1fb = cx_blpf = cx_ppmm = 0; break;
        default: break;
    }
}

// ---------- RIOT / memory map ----------
static uint8_t a2600_read(void* ctx, uint16_t addr) {
    a2600_t* m = (a2600_t*)ctx;
    if (addr < 0x80) {
        switch (addr & 0x1F) {
            case 0x00: return cx_m0p;
            case 0x01: return cx_m1p;
            case 0x02: return cx_p0fb;
            case 0x03: return cx_p1fb;
            case 0x04: return cx_m0fb;
            case 0x05: return cx_m1fb;
            case 0x06: return cx_blpf;
            case 0x07: return cx_ppmm;
            case 0x08: case 0x09: case 0x0A: case 0x0B:
                return 0x80;
            case 0x0C: case 0x0D:
                return fire0 ? 0x80 : 0;
            default: return 0xFF;
        }
    }
    if (addr >= 0x80 && addr < 0x280) return riot_ram[addr & 0x7F];
    if (addr >= 0x280 && addr < 0x300) {
        switch (addr & 0x1F) {
            case 0x00: return 0xF0 | (joy0 & 0x0F);
            case 0x02: return swchb;
            case 0x04: case 0x14: case 0x18: {
                uint32_t delta = (g_cc - timer_set_cc) >> timer_res;
                return (uint8_t)(timer_set_count - delta);
            }
            case 0x05: case 0x15: case 0x19: {
                uint32_t delta = (g_cc - timer_set_cc) >> timer_res;
                return (delta >= timer_set_count) ? 0xFF : 0x00;
            }
            default: return 0xFF;
        }
    }
    if (addr >= 0xF000) {
        if (m->rom_size <= 4096)
            return m->rom[(addr - 0xF000) & (m->rom_size - 1)];
        if (m->rom_size == 8192) {
            // F8: $F000-$F7FF = bank(m->bank), $F800-$FFFF = bank 1 (fixed)
            if (addr & 0x0800)
                return m->rom[1 * 4096 + ((addr - 0xF000) & 0xFFF)];
            return m->rom[m->bank * 4096 + ((addr - 0xF000) & 0xFFF)];
        }
        if (m->rom_size == 16384) {
            // F6: $F000-$F7FF = bank(m->bank), $F800-$FFFF = bank 3 (fixed)
            if (addr & 0x0800)
                return m->rom[3 * 4096 + ((addr - 0xF000) & 0xFFF)];
            return m->rom[m->bank * 4096 + ((addr - 0xF000) & 0xFFF)];
        }
        return m->rom[m->bank * 4096 + ((addr - 0xF000) & 0xFFF)];
    }
    return 0xFF;
}

static void a2600_write(void* ctx, uint16_t addr, uint8_t v) {
    a2600_t* m = (a2600_t*)ctx;
    if (addr < 0x80) { tia_write(addr & 0x3F, v); return; }
    if (addr >= 0x80 && addr < 0x280) { riot_ram[addr & 0x7F] = v; return; }
    if (addr >= 0x280 && addr < 0x300) {
        switch (addr & 0x1F) {
            case 0x14: timer_set_cc = g_cc; timer_set_count = v; timer_res = 0; break;
            case 0x15: timer_set_cc = g_cc; timer_set_count = v; timer_res = 3; break;
            case 0x16: timer_set_cc = g_cc; timer_set_count = v; timer_res = 6; break;
            case 0x17: timer_set_cc = g_cc; timer_set_count = v; timer_res = 10; break;
            default: break;
        }
        return;
    }
    if (m->rom_size == 8192) {
        if (addr == 0xFFF8) { m->bank = 0; return; }
        if (addr == 0xFFF9) { m->bank = 1; return; }
    }
    if (m->rom_size == 16384) {
        if (addr >= 0xFFF6 && addr <= 0xFFF9) { m->bank = addr - 0xFFF6; return; }
    }
}

// ---------- рендер строки ----------
// бит playfield по позиции тайла bi (0..19, слева направо):
//   0..3  = PF0 биты 4,5,6,7
//   4..11 = PF1 биты 7..0
//   12..19= PF2 биты 0..7
static int pf_bit(int bi) {
    if (bi < 4)    return (t_pf0 >> (4 + bi)) & 1;
    if (bi < 12)   return (t_pf1 >> (11 - bi)) & 1;
    return (t_pf2 >> (bi - 12)) & 1;
}

static void obj_copies(int nusize, int x0, int* xs, int* n) {
    switch (nusize & 7) {
        case 0: xs[0] = x0; *n = 1; break;
        case 1: xs[0] = x0; xs[1] = x0 + 16; *n = 2; break;
        case 2: xs[0] = x0; xs[1] = x0 + 32; *n = 2; break;
        case 3: xs[0] = x0; xs[1] = x0 + 16; xs[2] = x0 + 32; *n = 3; break;
        case 4: xs[0] = x0; xs[1] = x0 + 64; *n = 2; break;
        case 5: xs[0] = x0; *n = 1; break;  // double: бит = 2 пикселя
        case 6: xs[0] = x0; xs[1] = x0 + 32; xs[2] = x0 + 64; *n = 3; break;
        default: xs[0] = x0; *n = 1; break; // quad: бит = 4 пикселя
    }
}

static void render_line(uint16_t* line_out) {
    int reflect   = t_ctrlpf & 0x01; // D0
    int scores    = t_ctrlpf & 0x02; // D1
    int norm_prio = !(t_ctrlpf & 0x04); // D2
    int ball_w    = 1 << ((t_ctrlpf >> 4) & 3); // D4,D5: 1,2,4,8
    int ns0 = t_nusiz0 & 7, ns1 = t_nusiz1 & 7;
    int mw0 = 1 << ((t_nusiz0 >> 4) & 3); // ширина миссила 1,2,4,8
    int mw1 = 1 << ((t_nusiz1 >> 4) & 3);

    uint8_t colv[160];
    memset(colv, 0, sizeof(colv));

    // playfield (фон + стены)
    for (int x = 0; x < 160; x++) {
        int bi = (x < 80) ? x / 4 : (reflect ? 19 - (x - 80) / 4 : (x - 80) / 4);
        if (pf_bit(bi)) colv[x] |= PF_MASK;
    }

    // ball
    if (t_enabl && t_blx >= 0 && t_blx < 160) {
        for (int w = 0; w < ball_w && t_blx + w < 160; w++)
            colv[t_blx + w] |= BL_MASK;
    }

    // player 0 + копии, missile 0
    {
        int xs[3], n;
        obj_copies(ns0, t_p0x, xs, &n);
        uint8_t g0 = t_vdelp0 ? t_grp0_prev : t_grp0;
        if (t_refp0) { uint8_t r = 0; for (int b = 0; b < 8; b++) r |= ((g0 >> b) & 1) << (7 - b); g0 = r; }
        int wpx = (ns0 == 5) ? 2 : (ns0 == 7) ? 4 : 1;
        for (int c = 0; c < n; c++)
            for (int dx = 0; dx < 8 * wpx; dx++) {
                int xx = xs[c] + dx;
                if (xx < 0 || xx >= 160) continue;
                if (g0 & (0x80 >> (dx / wpx))) colv[xx] |= PL0_MASK;
            }
        if (t_enam0 && !t_resmp0 && t_m0x >= 0 && t_m0x < 160) {
            int mxs[3], mn;
            obj_copies(ns0, t_m0x, mxs, &mn);
            for (int c = 0; c < mn; c++)
                for (int w = 0; w < mw0; w++) {
                    int xx = mxs[c] + w;
                    if (xx >= 0 && xx < 160) colv[xx] |= ML0_MASK;
                }
        }
    }

    // player 1 + копии, missile 1
    {
        int xs[3], n;
        obj_copies(ns1, t_p1x, xs, &n);
        uint8_t g1 = t_vdelp1 ? t_grp1_prev : t_grp1;
        if (t_refp1) { uint8_t r = 0; for (int b = 0; b < 8; b++) r |= ((g1 >> b) & 1) << (7 - b); g1 = r; }
        int wpx = (ns1 == 5) ? 2 : (ns1 == 7) ? 4 : 1;
        for (int c = 0; c < n; c++)
            for (int dx = 0; dx < 8 * wpx; dx++) {
                int xx = xs[c] + dx;
                if (xx < 0 || xx >= 160) continue;
                if (g1 & (0x80 >> (dx / wpx))) colv[xx] |= PL1_MASK;
            }
        if (t_enam1 && !t_resmp1 && t_m1x >= 0 && t_m1x < 160) {
            int mxs[3], mn;
            obj_copies(ns1, t_m1x, mxs, &mn);
            for (int c = 0; c < mn; c++)
                for (int w = 0; w < mw1; w++) {
                    int xx = mxs[c] + w;
                    if (xx >= 0 && xx < 160) colv[xx] |= ML1_MASK;
                }
        }
    }

    // разрешение приоритета + цвет + коллизии
    for (int x = 0; x < 160; x++) {
        uint8_t cv = colv[x];
        int colind;
        if (norm_prio) {
            if (cv & (PL0_MASK | ML0_MASK))      colind = 0; // P0/M0
            else if (cv & (PL1_MASK | ML1_MASK)) colind = 1; // P1/M1
            else if (cv & (BL_MASK | PF_MASK))   colind = 2; // Ball/PF
            else colind = 3;                                  // фон
        } else {
            if (cv & (BL_MASK | PF_MASK))        colind = 2;
            else if (cv & (PL0_MASK | ML0_MASK)) colind = 0;
            else if (cv & (PL1_MASK | ML1_MASK)) colind = 1;
            else colind = 3;
        }
        uint8_t colbyte;
        switch (colind) {
            case 0: colbyte = t_colup0; break;
            case 1: colbyte = t_colup1; break;
            case 2: colbyte = scores ? (x < 80 ? t_colup0 : t_colup1) : t_colupf; break;
            default: colbyte = t_colubk; break;
        }
        line_out[x] = ntsc_pal[colbyte & 0x7F];

        // collision latches (биты как в железе: D7/D6)
        if (cv & ML0_MASK) {
            if (cv & PL0_MASK) cx_m0p |= 0x80;
            if (cv & PL1_MASK) cx_m0p |= 0x40;
            if (cv & PF_MASK)  cx_m0fb |= 0x80;
            if (cv & BL_MASK)  cx_m0fb |= 0x40;
        }
        if (cv & ML1_MASK) {
            if (cv & PL1_MASK) cx_m1p |= 0x80;
            if (cv & PL0_MASK) cx_m1p |= 0x40;
            if (cv & PF_MASK)  cx_m1fb |= 0x80;
            if (cv & BL_MASK)  cx_m1fb |= 0x40;
        }
        if (cv & PL0_MASK) {
            if (cv & PF_MASK) cx_p0fb |= 0x80;
            if (cv & BL_MASK) cx_p0fb |= 0x40;
            if (cv & PL1_MASK) cx_ppmm |= 0x80;
        }
        if (cv & PL1_MASK) {
            if (cv & PF_MASK) cx_p1fb |= 0x80;
            if (cv & BL_MASK) cx_p1fb |= 0x40;
        }
        if (cv & BL_MASK && cv & PF_MASK) cx_blpf |= 0x40;
        if (cv & ML0_MASK && cv & ML1_MASK) cx_ppmm |= 0x40;
    }
}

void a2600_init(a2600_t* m, const uint8_t* rom, uint32_t size, uint16_t* fb) {
    if (!pal_init) { init_palette(); pal_init = 1; }

    t_vsync = t_vblank = 0;
    t_pf0 = t_pf1 = t_pf2 = 0; t_ctrlpf = 0;
    t_colup0 = t_colup1 = t_colum0 = t_colum1 = t_colubk = t_colupf = 0;
    t_grp0 = t_grp1 = t_grp0_prev = t_grp1_prev = 0;
    t_enam0 = t_enam1 = t_enabl = 0;
    t_nusiz0 = t_nusiz1 = 0;
    t_refp0 = t_refp1 = 0;
    t_vdelp0 = t_vdelp1 = 0;
    t_resmp0 = t_resmp1 = 0;
    t_hm0 = t_hm1 = t_hmm0 = t_hmm1 = t_hmbl = 0;
    t_p0x = t_p1x = t_m0x = t_m1x = t_blx = 0;
    cx_m0p = cx_m1p = cx_p0fb = cx_p1fb = cx_m0fb = cx_m1fb = cx_blpf = cx_ppmm = 0;
    memset(riot_ram, 0, sizeof(riot_ram));
    timer_set_cc = 0; timer_set_count = 0; timer_res = 0;
    g_cc = 0;
    joy0 = 0xFF; fire0 = 1; swchb = 0x7F;
    frame_index = 0;
    wsync_flag = 0;

    memset(m, 0, sizeof(*m));
    m->rom = (uint8_t*)rom;
    m->rom_size = size;
    m->fb = fb;
    m->bank = 0;
    if (size == 8192) m->bank = 1;
    else if (size == 16384) m->bank = 3;
    switch (size) {
        case 2048:  m->mapper_label = "2K (NROM)";   break;
        case 4096:  m->mapper_label = "4K (NROM)";   break;
        case 8192:  m->mapper_label = "8K (F8)";     break;
        case 16384: m->mapper_label = "16K (F6)";    break;
        case 32768: m->mapper_label = "32K (F4)";    break;
        default:    m->mapper_label = "?K";          break;
    }
    m->cpu.read = a2600_read;
    m->cpu.write = a2600_write;
    m->cpu.mem_ctx = m;
    cpu6502_init(&m->cpu);
    t_colubk = 0;
}

void a2600_set_input(uint8_t joy0_dir, uint8_t joy0_fire) {
    // SWCHA: D3=up, D2=down, D1=left, D0=right. Бит = 0 означает нажатие.
    joy0 = 0xFF;
    if (joy0_dir & 1) joy0 &= ~0x08;  // up
    if (joy0_dir & 2) joy0 &= ~0x04;  // down
    if (joy0_dir & 4) joy0 &= ~0x02;  // left
    if (joy0_dir & 8) joy0 &= ~0x01;  // right
    // INPT4/INPT5: бит 7 = 0 при нажатии
    fire0 = joy0_fire ? 0 : 1;
}

void a2600_get_video(uint8_t out[8]) {
    out[0] = t_vblank;
    out[1] = t_vsync;
    out[2] = t_colubk;
    out[3] = t_colupf;
    out[4] = t_grp0;
    out[5] = t_grp1;
    out[6] = t_pf0;
    out[7] = t_pf1;
}

// счётчик инструкций и отрендеренных строк за кадр (диагностика)
uint32_t g_frame_insns = 0;
uint32_t g_frame_lines = 0;
uint32_t g_frame_vbl = 0;
uint32_t g_frame_vsync = 0;

void a2600_frame(a2600_t* m) {
    uint16_t line[160];
    memset(m->fb, 0, 160 * VIS_LINES * 2);
    // flush D-cache
    {
        uint32_t a = (uint32_t)m->fb & ~0x1Fu;
        uint32_t end = a + 160 * VIS_LINES * 2 + 32;
        for (; a < end; a += 32)
            __asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a));
        __asm volatile("dsb" ::: "memory");
    }
    wsync_flag = 0;
    g_frame_insns = 0;
    g_frame_lines = 0;
    g_frame_vbl = 0;
    g_frame_vsync = 0;
    m->cpu.cpu.cycles = 0;

    uint32_t target_cc = g_cc + TOTAL_LINES * 228u;  // 262 * 228 = 59736 CC

    while (g_cc < target_cc) {
        uint32_t L0 = g_cc / 228u;   // строка до инструкции

        uint32_t before = m->cpu.cpu.cycles;
        cpu6502_exec(&m->cpu, m->cpu.cpu.cycles + 1);
        g_frame_insns++;
        uint32_t elapsed = m->cpu.cpu.cycles - before;
        if (elapsed == 0) elapsed = 1;
        g_cc += elapsed * 3;

        if (wsync_flag) {
            // CPU "засыпает" до конца строки: луч мгновенно завершает строку
            wsync_flag = 0;
            g_cc = ((g_cc / 228u) + 1) * 228u;
        }

        // рендер всех строк, завершённых с момента прошлой итерации,
        // с СОСТОЯНИЕМ РЕГИСТРОВ НА ЭТУ СТРОКУ (как в железе)
        uint32_t L1 = g_cc / 228u;
        for (uint32_t L = L0; L < L1; L++) {
            int ll = (int)(L % TOTAL_LINES);
            if (ll >= VIS_START && ll < VIS_START + VIS_LINES) {
                if (t_vblank & 0x02) g_frame_vbl++;
                if (t_vsync & 0x02) g_frame_vsync++;
                if (!(t_vblank & 0x02) && !(t_vsync & 0x02)) {
                    render_line(line);
                    memcpy(m->fb + (ll - VIS_START) * 160, line, 160 * 2);
                    g_frame_lines++;
                }
            }
        }
    }
    frame_index++;
}