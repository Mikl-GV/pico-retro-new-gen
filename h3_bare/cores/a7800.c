#include "a7800.h"
#include <string.h>
#include <stdbool.h>

// ============================================================
// MARIA palette registers вЂ” Р РђР—Р Р•Р–Р•РќРќР«Р•: $21,$25,$29,$2D,$31,$35,$39,$3D
// (РёРЅРґРµРєСЃС‹ РІРЅСѓС‚СЂРё maria[]: 0x01,0x05,0x09,0x0D,0x11,0x15,0x19,0x1D)
// ============================================================
static inline uint8_t pal_reg(int pal, int idx) {
    static const uint8_t base[8] = {0x01,0x05,0x09,0x0D,0x11,0x15,0x19,0x1D};
    return base[pal & 7] + (idx - 1);
}

static inline uint16_t col16(uint8_t c) {
    // СѓРїСЂРѕС‰С‘РЅРЅРѕ: RGB565 РёР· hue|lum
    return (uint16_t)(((uint32_t)(c & 0xF) * 31 / 15) << 11) |
           (uint16_t)(((uint32_t)(c >> 4 & 7) * 63 / 7) << 5);
}

// ============================================================
// РџР°РјСЏС‚СЊ 7800
// ============================================================
uint8_t a7800_read(void* ctx, uint16_t addr) {
    a7800_t* m = (a7800_t*)ctx;
    if (addr < 0x0040) return (addr < 0x20) ? m->tia[addr] : m->maria[addr - 0x20];
    if (addr >= 0x0040 && addr < 0x0100) return m->ram_low[addr - 0x0040];
    if (addr >= 0x0100 && addr < 0x0120) return m->tia[addr - 0x0100];
    if (addr >= 0x0120 && addr < 0x0140) return m->maria[addr - 0x0120];
    if (addr >= 0x0140 && addr < 0x0200) return m->ram_low[0x40 + addr - 0x0140];
    if (addr >= 0x0200 && addr < 0x0220) return m->tia[addr - 0x0200];
    if (addr >= 0x0220 && addr < 0x0240) return m->maria[addr - 0x0220];
    if (addr >= 0x0280 && addr < 0x0300) return 0xFF;
    if (addr >= 0x0480 && addr < 0x0500) return m->riot_ram[addr - 0x0480];
    if (addr >= 0x1800 && addr < 0x2800) return m->ram_main[addr - 0x1800];

    if (addr >= 0x4000) {
        if (m->bios && addr >= m->bios_base && addr < m->bios_base + m->bios_size)
            return m->bios[addr - m->bios_base];
        if (addr >= 0xC000 && m->rom_size >= 16384) {
            uint32_t off = (addr - 0xC000) + (m->rom_size - 16384);
            if (off < m->rom_size) return m->rom[off];
            return 0xFF;
        }
        if (addr < 0xC000) {
            uint32_t off = (addr - 0x4000) % m->rom_size;
            if (off < m->rom_size) return m->rom[off];
        }
        return 0xFF;
    }
    return 0xFF;
}

void a7800_write(void* ctx, uint16_t addr, uint8_t v) {
    a7800_t* m = (a7800_t*)ctx;
    if (addr < 0x0040) {
        if (addr < 0x20) { m->tia[addr] = v; return; }
        int r = addr - 0x20;
        m->maria[r] = v;
        if (r == 0x0C) m->dll_ptr = (m->dll_ptr & 0x00FF) | ((uint16_t)v << 8);
        if (r == 0x10) m->dll_ptr = (m->dll_ptr & 0xFF00) | v;
        return;
    }
    if (addr >= 0x0100 && addr < 0x0120) { m->tia[addr - 0x0100] = v; return; }
    if (addr >= 0x0120 && addr < 0x0140) {
        int r = addr - 0x0120;
        m->maria[r] = v;
        if (r == 0x0C) m->dll_ptr = (m->dll_ptr & 0x00FF) | ((uint16_t)v << 8);
        if (r == 0x10) m->dll_ptr = (m->dll_ptr & 0xFF00) | v;
        return;
    }
    if (addr >= 0x0040 && addr < 0x0100) { m->ram_low[addr - 0x0040] = v; return; }
    if (addr >= 0x0140 && addr < 0x0200) { m->ram_low[0x40 + addr - 0x0140] = v; return; }
    if (addr >= 0x0200 && addr < 0x0220) { m->tia[addr - 0x0200] = v; return; }
    if (addr >= 0x0220 && addr < 0x0240) {
        int r = addr - 0x0220;
        m->maria[r] = v;
        if (r == 0x0C) m->dll_ptr = (m->dll_ptr & 0x00FF) | ((uint16_t)v << 8);
        if (r == 0x10) m->dll_ptr = (m->dll_ptr & 0xFF00) | v;
        return;
    }
    if (addr >= 0x0480 && addr < 0x0500) { m->riot_ram[addr - 0x0480] = v; return; }
    if (addr >= 0x1800 && addr < 0x2800) { m->ram_main[addr - 0x1800] = v; return; }
}

// ============================================================
// Р РµРЅРґРµСЂ РѕРґРЅРѕР№ СЃС‚СЂРѕРєРё РёР· DL
// ============================================================
static void render_dl_line(a7800_t* m, uint16_t dl_addr, int offset, uint16_t* out) {
    uint8_t ctrl = m->maria[0x1C];
    bool kangaroo = (ctrl & 0x04);
    uint16_t bg = col16(m->maria[0x00]);

    for (int x = 0; x < 320; x++) out[x] = bg;

    int idx = 0;
    while (idx < 256) {
        uint8_t b0 = a7800_read(&m->cpu, dl_addr + idx);
        uint8_t b1 = a7800_read(&m->cpu, dl_addr + idx + 1);
        uint8_t b2 = a7800_read(&m->cpu, dl_addr + idx + 2);
        uint8_t b3 = a7800_read(&m->cpu, dl_addr + idx + 3);
        if (b0 == 0 && b1 == 0 && b2 == 0) break;

        uint8_t pal = (b1 >> 5) & 7;
        uint8_t w   = (b1 & 0x1F) + 1;
        uint16_t gfx = (uint16_t)b0 | ((uint16_t)b2 << 8);
        uint8_t hpos = b3;

        // extended (WM=1, bit7 b1) вЂ” РїСЂРѕРїСѓСЃРєР°РµРј
        if (b1 & 0x80) { idx += 5; continue; }

        uint16_t data_row = gfx + (offset << 8);
        uint8_t rm = ctrl & 0x03;
        bool is_320 = (rm == 2 || rm == 3);
        uint32_t cols[4] = {
            0,
            col16(m->maria[pal_reg(pal,1)]),
            col16(m->maria[pal_reg(pal,2)]),
            col16(m->maria[pal_reg(pal,3)]),
        };
        cols[0] = bg;

        if (is_320) {
            for (int bi = 0; bi < w; bi++) {
                uint8_t g = a7800_read(&m->cpu, data_row + bi);
                for (int bit = 0; bit < 8; bit++) {
                    uint8_t pen = (g >> (7-bit)) & 1;
                    if (!pen && !kangaroo) continue;
                    int px = hpos + bi*8 + bit;
                    if (px >= 0 && px < 320) out[px] = (uint16_t)cols[pen];
                }
            }
        } else {
            for (int bi = 0; bi < w; bi++) {
                uint8_t g = a7800_read(&m->cpu, data_row + bi);
                for (int p = 0; p < 4; p++) {
                    uint8_t pen = (g >> (6-p*2)) & 3;
                    if (!pen && !kangaroo) continue;
                    int px = hpos + bi*8 + p*2;
                    if (px >= 0 && px < 320) {
                        out[px] = (uint16_t)cols[pen];
                        if (px+1 < 320) out[px+1] = (uint16_t)cols[pen];
                    }
                }
            }
        }
        idx += 4;
    }
}

// ============================================================
// РљР°РґСЂ: РѕР±С…РѕРґ DLL
// ============================================================
static void render_frame(a7800_t* m) {
    uint16_t dll = m->dll_ptr;
    uint16_t bg = col16(m->maria[0x00]);
    int line = 0, dll_idx = 0;

    if (!dll || !m->fb) {
        for (int y = 0; y < A7800_TV_H; y++)
            for (int x = 0; x < A7800_TV_W; x++) m->fb[y*A7800_TV_W + x] = 0;
        return;
    }

    while (line < A7800_TV_H && dll_idx < 512) {
        uint8_t d0 = a7800_read(&m->cpu, dll + dll_idx);
        uint8_t d1 = a7800_read(&m->cpu, dll + dll_idx + 1);
        uint8_t d2 = a7800_read(&m->cpu, dll + dll_idx + 2);
        uint8_t offset = d0 & 0x0F;
        uint16_t dl_addr = ((uint16_t)d1 << 8) | d2;

        for (int z = 0; z <= offset && line < A7800_TV_H; z++) {
            render_dl_line(m, dl_addr, offset - z, m->fb + line * A7800_TV_W);
            line++;
        }
        dll_idx += 3;
    }
    for (; line < A7800_TV_H; line++) {
        for (int x = 0; x < A7800_TV_W; x++) m->fb[line*A7800_TV_W + x] = bg;
    }
}

// ============================================================
// API
// ============================================================
void a7800_init(a7800_t* m, const uint8_t* rom, uint32_t size,
                const uint8_t* bios, uint16_t bios_size, uint16_t* fb) {
    memset(m, 0, sizeof(*m));
    m->rom = rom; m->rom_size = size;
    m->bios = bios; m->bios_base = 0xF000; m->bios_size = bios_size;
    m->fb = fb;
    m->cpu.ctx = m; m->cpu.read = a7800_read; m->cpu.write = a7800_write;
    a7800_reset(m);
}

void a7800_reset(a7800_t* m) {
    memset(m->ram_main, 0, 0x1000);
    memset(m->ram_low, 0, 256);
    memset(m->riot_ram, 0, 128);
    m->frame_done = 0;
    m->dll_ptr = 0;
    cpu6502_reset(&m->cpu);   // С‡РёС‚Р°РµС‚ $FFFC/$FFFD РёР· ROM (РІРµРєС‚РѕСЂС‹ РІ РєР°СЂС‚СЂРёРґР¶Рµ)
}

void a7800_frame(a7800_t* m) {
    cpu6502_run(&m->cpu, A7800_FRAME_CYCLES);
    render_frame(m);
    m->frame++;
    m->frame_done = 1;
}