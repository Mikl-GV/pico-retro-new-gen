#include "a5200.h"
#include <string.h>

// ============================================================
// РџР°СЂР°РјРµС‚СЂС‹ ANTIC-СЂРµР¶РёРјРѕРІ: scanlines, wide(160=1,320=2), bpp, is_char
// ============================================================
typedef struct {
    uint8_t sl, w, bpp;
    int is_char;
} am_t;

static const am_t am[16] = {
    {1,0,0,0},  {1,0,0,0},  {8,1,1,1},  {10,1,1,1},
    {8,1,2,1},  {16,1,2,1}, {8,1,1,1},  {16,1,1,1},
    {8,1,2,0},  {4,1,2,0},  {4,1,2,0},  {2,1,1,0},
    {1,1,1,0},  {2,1,2,0},  {1,1,2,0},  {1,2,1,0},
};

// Цвет GTIA (hue|lum) -> RGB565. Полная NTSC-палитра Atari 8-bit,
// с синим каналом (иначе белый/серый уходят в жёлтый).
static inline uint16_t col16(uint8_t c) {
    static const uint8_t rgb[16][3] = {
        {0x00,0x00,0x00},{0x90,0x70,0x20},{0xB0,0x60,0x10},{0xC0,0x40,0x20},
        {0xD0,0x20,0x20},{0xB0,0x20,0x80},{0x80,0x30,0xA0},{0x40,0x40,0xC0},
        {0x20,0x60,0xD0},{0x20,0x90,0xE0},{0x20,0xA0,0xB0},{0x30,0xB0,0x60},
        {0x60,0xC0,0x40},{0xA0,0xB0,0x30},{0xC0,0x80,0x30},{0xC0,0xC0,0xC0},
    };
    uint8_t h = (c >> 4) & 0x0F;
    uint8_t l = c & 0x0F;
    uint8_t rr = (uint16_t)rgb[h][0] * l / 15;
    uint8_t gg = (uint16_t)rgb[h][1] * l / 15;
    uint8_t bb = (uint16_t)rgb[h][2] * l / 15;
    return (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
}

// ============================================================
// РџР°РјСЏС‚СЊ 5200
// ============================================================
uint8_t a5200_read(void* ctx, uint16_t addr) {
    a5200_t* m = (a5200_t*)ctx;
    if (addr < 0x4000) return m->ram[addr];
    if (m->bios && addr >= 0xF000) return m->bios[addr - 0xF000];
    if (addr >= 0xE800 && addr < 0xE840) return m->pokey[addr & 0x3F];
    if (addr >= 0xD000 && addr < 0xD040) return m->antic[addr & 0x3F];
    if (addr >= 0xC000 && addr < 0xC040) return m->gtia[addr & 0x3F];
    if (addr >= 0x4000 && addr < 0x8000) {
        uint32_t off = addr - 0x4000;
        return (off < m->rom_size) ? m->rom[off] : 0xFF;
    }
    return 0xFF;
}

void a5200_write(void* ctx, uint16_t addr, uint8_t v) {
    a5200_t* m = (a5200_t*)ctx;
    if (addr < 0x4000) { m->ram[addr] = v; return; }
    if (addr >= 0xC000 && addr < 0xC040) { m->gtia[addr & 0x3F] = v; return; }
    if (addr >= 0xD000 && addr < 0xD040) {
        m->antic[addr & 0x3F] = v;
        switch (addr & 0x3F) {
        case 0x02: m->dlist = (m->dlist & 0xFF00) | v; break;
        case 0x03: m->dlist = (m->dlist & 0x00FF) | ((uint16_t)v << 8); break;
        case 0x07: m->pmbase = v; break;
        case 0x09: m->chbase = v; break;
        case 0x00: m->dmactl = v; break;
        }
        return;
    }
    if (addr >= 0xE800 && addr < 0xE840) { m->pokey[addr & 0x3F] = v; return; }
}

// ============================================================
// Р РµРЅРґРµСЂ display list РІ framebuffer fb[320 x 192]
// ============================================================
static void render_frame(a5200_t* m) {
    uint8_t* ram = m->ram;
    uint16_t dlist = m->dlist;
    int line = 0, dl_idx = 0;
    uint16_t data_addr = 0;

    uint8_t colbk = m->gtia[0x1A];
    uint8_t colpf[4] = { colbk, m->gtia[0x16], m->gtia[0x17], m->gtia[0x18] };

    while (line < 192 && dl_idx < 1024) {
        uint8_t instr = ram[dlist + dl_idx];
        if (instr == 0) { dl_idx++; line++; continue; }
        if (instr == 1 || instr == 0x41) {
            dl_idx++;
            uint16_t lo = ram[dlist + dl_idx++];
            uint16_t hi = ram[dlist + dl_idx++];
            if (instr == 0x41) break;
            dlist = (hi << 8) | lo;
            dl_idx = 0;
            continue;
        }
        uint8_t mode = instr & 0x0F;
        int lms = instr & 0x40;
        dl_idx++;
        if (lms) {
            data_addr = ram[dlist + dl_idx] | ((uint16_t)ram[dlist + dl_idx + 1] << 8);
            dl_idx += 2;
        }
        const am_t* md = &am[mode];
        int bpr = md->is_char ? 0 : (md->bpp == 1 ? (md->w == 2 ? 40 : 20) : 40);

        for (int s = 0; s < md->sl && line < 192; s++, line++) {
            if (!m->fb) break;
            uint16_t* out = m->fb + line * 320;
            uint16_t bg = col16(colbk);
            for (int x = 0; x < 320; x++) out[x] = bg;

            uint16_t row_addr = md->is_char ? data_addr : data_addr + s * bpr;

            if (md->is_char) {
                int char_h = md->sl;
                uint16_t cs = (uint16_t)m->chbase << 8;
                for (int c = 0; c < md->w * 20; c++) {
                    uint8_t ch = ram[row_addr + c];
                    uint8_t glyph = ram[cs + ch * char_h + s];
                    for (int b = 0; b < 8; b++) {
                        int px = c * 8 + b;
                        if (px < 320)
                            out[px] = (glyph & (0x80 >> b)) ? col16(colpf[1]) : bg;
                    }
                }
            } else if (md->bpp == 1 && md->w == 2) {
                for (int i = 0; i < 40; i++) {
                    for (int bit = 0; bit < 8; bit++) {
                        int px = i * 8 + bit;
                        if (px < 320)
                            out[px] = col16(colpf[(ram[row_addr + i] >> (7 - bit)) & 1]);
                    }
                }
            } else if (md->bpp == 1) {
                for (int i = 0; i < 20; i++) {
                    for (int bit = 0; bit < 8; bit++) {
                        int px = i * 8 + bit;
                        if (px < 160)
                            out[px] = col16(colpf[(ram[row_addr + i] >> (7 - bit)) & 1]);
                    }
                }
            } else if (md->bpp == 2) {
                for (int i = 0; i < 40; i++) {
                    for (int p = 0; p < 4; p++) {
                        int px = i * 4 + p;
                        if (px < 160)
                            out[px] = col16(colpf[(ram[row_addr + i] >> (6 - p * 2)) & 3]);
                    }
                }
            }

            // PMG РїРѕРІРµСЂС… (СѓРїСЂРѕС‰С‘РЅРЅРѕ: С‚РѕР»СЊРєРѕ РёРіСЂРѕРєРё, 1x С€РёСЂРёРЅР°)
            if ((m->dmactl & 0x0C) && (m->gtia[0x1D] & 0x02)) {
                uint16_t pm_addr = (m->pmbase & 0xF8) << 8;
                for (int p = 0; p < 4; p++) {
                    uint8_t pdata = ram[pm_addr + 0x0400 + p * 0x100 + line];
                    if (!pdata) continue;
                    int sx = m->gtia[p];
                    uint8_t colpm = m->gtia[0x12 + p];
                    for (int b = 0; b < 8; b++) {
                        if (!(pdata & (0x80 >> b))) continue;
                        int px = sx + b;
                        if (px >= 0 && px < 320) out[px] = col16(colpm);
                    }
                }
            }
        }
        data_addr += md->is_char ? (md->w * 20) : md->sl * bpr;
    }
}

// ============================================================
// API
// ============================================================
void a5200_init(a5200_t* m, const uint8_t* rom, uint32_t size,
                const uint8_t* bios, uint16_t* fb) {
    memset(m, 0, sizeof(*m));
    m->rom = rom; m->rom_size = size; m->bios = bios; m->fb = fb;
    m->cpu.ctx = m; m->cpu.read = a5200_read; m->cpu.write = a5200_write;
    a5200_reset(m);
}

void a5200_reset(a5200_t* m) {
    memset(m->ram, 0, 0x4000);
    m->frame_done = 0;
    cpu6502_reset(&m->cpu);   // С‡РёС‚Р°РµС‚ $FFFC/$FFFD РёР· BIOS
}

void a5200_frame(a5200_t* m) {
    cpu6502_run(&m->cpu, A5200_FRAME_CYCLES);
    render_frame(m);
    m->frame++;
    m->frame_done = 1;
}