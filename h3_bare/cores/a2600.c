#include "a2600.h"
#include <string.h>

// ============================================================
// Р¦РІРµС‚ TIA -> RGB565 (NTSC-РїСЂРёР±Р»РёР¶РµРЅРёРµ)
// ============================================================
static uint16_t tia_color(uint8_t r) {
    static const uint8_t hue[8][3] = {
        {0x00,0x00,0x00},{0x90,0x30,0x30},{0xA0,0x70,0x20},{0x90,0x90,0x20},
        {0x30,0x90,0x30},{0x20,0x80,0x90},{0x40,0x40,0x90},{0x90,0x40,0x90},
    };
    uint8_t h = (r >> 4) & 7, l = r & 0x0F;
    uint8_t rr = (uint16_t)hue[h][0] * l / 15;
    uint8_t gg = (uint16_t)hue[h][1] * l / 15;
    uint8_t bb = (uint16_t)hue[h][2] * l / 15;
    return (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
}

// ============================================================
// РџРѕР·РёС†РёСЏ РЅР° СЃС‚СЂРѕРєРµ: РїСЂРѕС€Р»Рѕ (76-cycles) С†РёРєР»РѕРІ -> 0..227
// ============================================================
static inline int color_clock(const a2600_t* m) {
    int el = 76 - m->cpu.cycles;
    if (el < 0) el = 0;
    if (el > 76) el = 76;
    return (el * 3) % 228;
}

static inline int res_pixel(const a2600_t* m) {
    int cc = color_clock(m) - 68;
    if (cc < 0) cc = 0;
    if (cc > 159) cc = 159;
    return cc;
}

// ============================================================
// РџР°РјСЏС‚СЊ A2600
// ============================================================
uint8_t a2600_read(void* ctx, uint16_t addr) {
    a2600_t* m = (a2600_t*)ctx;
    if (addr >= 0x1000) {
        uint32_t off = addr & 0x0FFF;
        if (off < m->rom_size) return m->rom[off];
        return 0xFF;
    }
    if (addr & 0x80) {                       // RIOT
        if (addr & 0x200) {                  // СЂРµРіРёСЃС‚СЂС‹ $280-$2FF
            switch (addr & 0x7F) {
            case 0x00: return m->swcha;
            case 0x01: return m->swchb;
            default:   return 0;
            }
        }
        return m->ram[addr & 0x7F];
    }
    // TIA read: INPT (D7=1 = РЅРµ РЅР°Р¶Р°С‚Рѕ)
    switch (addr & 0x3F) {
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: return 0x80;
    default: return 0;
    }
}

void a2600_write(void* ctx, uint16_t addr, uint8_t v) {
    a2600_t* m = (a2600_t*)ctx;
    if (addr >= 0x1000) return;              // РІС‹Р±РѕСЂ Р±Р°РЅРєР° вЂ” С‚РѕР»СЊРєРѕ 4K-РёРіСЂС‹
    if (addr & 0x80) {                       // RIOT
        if (addr & 0x200) {
            uint8_t r = addr & 0x7F;
            if (r == 0x00) m->swcha = v;
            return;
        }
        m->ram[addr & 0x7F] = v;
        return;
    }
    uint8_t r = addr & 0x3F;
    m->regs[r] = v;
    switch (r) {
    case 0x02:  m->cpu.cycles = -0x40000000; break;  // WSYNC
    case 0x10:  m->p0_x = res_pixel(m); break;
    case 0x11:  m->p1_x = res_pixel(m); break;
    case 0x12:  m->m0_x = res_pixel(m); break;
    case 0x13:  m->m1_x = res_pixel(m); break;
    case 0x14:  m->bl_x = res_pixel(m); break;
    case 0x27:  // HMOVE
        m->p0_x += ((m->regs[0x20] >> 4) & 0x0F) - 8;
        m->p1_x += ((m->regs[0x21] >> 4) & 0x0F) - 8;
        m->m0_x += ((m->regs[0x22] >> 4) & 0x0F) - 8;
        m->m1_x += ((m->regs[0x23] >> 4) & 0x0F) - 8;
        m->bl_x += ((m->regs[0x24] >> 4) & 0x0F) - 8;
        break;
    case 0x28:  // HMCLR
        m->regs[0x20]=m->regs[0x21]=m->regs[0x22]=m->regs[0x23]=m->regs[0x24]=0x80;
        break;
    default: break;
    }
}

// ============================================================
// Р РµРЅРґРµСЂ РѕРґРЅРѕР№ СЃС‚СЂРѕРєРё РІ fb[scanline * 160]
// ============================================================
static void render_line(a2600_t* m) {
    if (!m->fb) return;
    uint8_t* R = m->regs;
    uint16_t* out = m->fb + (m->scanline - 38) * 160;
    // С‚С‘РјРЅС‹Рµ СЃС‚СЂРѕРєРё (РІРµСЂС…/РЅРёР·) вЂ” Р·Р°РїРѕР»РЅСЏРµС‚СЃСЏ С‡С‘СЂРЅС‹Рј РІ frame()

    uint8_t ctrlpf = R[0x0A];
    int reflect = ctrlpf & 0x01;
    int score   = (ctrlpf >> 2) & 1;
    int prior   = (ctrlpf >> 3) & 1;

    uint16_t c_bk = tia_color(R[0x09]);
    uint16_t c_pf = tia_color(R[0x08]);
    uint16_t c_p0 = tia_color(R[0x06]);
    uint16_t c_p1 = tia_color(R[0x07]);

    // playfield 20 Р±РёС‚
    uint8_t pf_bits[20];
    for (int i = 0; i < 4; i++)  pf_bits[i]    = (R[0x0D] >> (3 - i)) & 1;
    for (int i = 0; i < 8; i++)  pf_bits[4+i]  = (R[0x0E] >> (7 - i)) & 1;
    for (int i = 0; i < 8; i++)  pf_bits[12+i] = (R[0x0F] >> (7 - i)) & 1;

    uint8_t grp0 = R[0x1B], grp1 = R[0x1C];
    int p0_w = ((R[0x04] & 0x07) == 0) ? 1 : (((R[0x04] & 0x07) == 1) ? 2 : 4);
    int p1_w = ((R[0x05] & 0x07) == 0) ? 1 : (((R[0x05] & 0x07) == 1) ? 2 : 4);
    int p0x = m->p0_x, p1x = m->p1_x;

    for (int x = 0; x < 160; x++) {
        uint16_t c = c_bk;

        int half = x / 80;
        int sub  = x % 80;
        int biti = sub / 4;
        if (half) biti = reflect ? (19 - biti) : biti;
        if (pf_bits[biti]) {
            c = c_pf;
            if (score) c = half ? c_p1 : c_p0;   // score: P0=Р»РµРІРѕ, P1=РїСЂР°РІРѕ
        }

        // РёРіСЂРѕРєРё
        if (!prior || !pf_bits[biti]) {
            int rel = x - p0x;
            if (rel >= 0 && rel < 8 * p0_w) {
                int bit = rel / p0_w;
                if (R[0x0B] & 1) bit = 7 - bit;
                if ((grp0 >> bit) & 1) c = c_p0;
            }
            int rel1 = x - p1x;
            if (rel1 >= 0 && rel1 < 8 * p1_w) {
                int bit = rel1 / p1_w;
                if (R[0x0C] & 1) bit = 7 - bit;
                if ((grp1 >> bit) & 1) c = c_p1;
            }
        }

        out[x] = c;
    }
}

// ============================================================
// API
// ============================================================
void a2600_init(a2600_t* m, const uint8_t* rom, uint32_t size, uint16_t* fb) {
    memset(m, 0, sizeof(*m));
    m->rom = rom;
    m->rom_size = size;
    m->fb = fb;
    m->cpu.ctx = m;
    m->cpu.read = a2600_read;
    m->cpu.write = a2600_write;
    a2600_reset(m);
}

void a2600_reset(a2600_t* m) {
    memset(m->ram, 0, 128);
    m->swcha = 0xFF;
    m->swchb = 0xFF;
    m->scanline = 0;
    m->frame_done = 0;
    m->p0_x = m->p1_x = m->m0_x = m->m1_x = m->bl_x = 0;
    cpu6502_reset(&m->cpu);
}

// РљР°РґСЂ: 262 СЃС‚СЂРѕРєРё. Р’РёРґРёРјС‹Рµ 38..229 (192 СЃС‚СЂРѕРєРё)
void a2600_frame(a2600_t* m) {
    for (int s = 0; s < A2600_SCANLINES; s++) {
        m->scanline = s;
        cpu6502_run(&m->cpu, A2600_CYCLES_LINE);
        int vis = s - 38;
        if (vis >= 0 && vis < 192) {
            if (m->fb) memset(m->fb + vis * 160, 0, 160 * 2);
            render_line(m);
        }
    }
    m->frame++;
    m->frame_done = 1;
}