/* pxt_host.c — IBM PC/XT-286 (pico-286) на bare-metal H3.
 *
 * Ядро ported из emulator-refs/pico-286: src/emulator/*.c (чистый C,
 * ресint в pxt/src/emulator). Этот host:
 *   - назначает read86/write86 = *_ob (RAM в DRAM, 640 КБ, без PSRAM/swap);
 *   - настраивает диски через pxt_disk_set (RAM-образы, см. disks-h3.c.inl);
 *   - рендерит VGA 640x480 (renderer() из linux-main, портирован как C)
 *     в RGB32-буфер, затем масштабирует в EMU_FB (RGB565 320x240);
 *   - звук: get_sound_sample() даёт стерео 44100 int16 → rs44fs_stereo → i2s.
 *
 * Цикл like linux-main: exec86(tormoz) → рендер 60 Гц → звук → emu_throttle.
 * Выход: emu_esc_hold.
 */

#include <stdint.h>
#include <string.h>

#include "emulator/emulator.h"
#include "emulator/includes/font8x8.h"
#include "emulator/includes/font8x16.h"

#include "emu.h"
#include "emulog.h"
#include "usb_kbd.h"
#include "h3_hs_timer.h"
#include "i2s.h"
#include "resampler44.h"
#include "fb_text.h"
#include "fat.h"

extern void udelay(uint32_t d);
extern void ems_reset(void);

extern int printf(const char *fmt, ...);

// ---------------------------------------------------------------------------
// Рендер (перенос renderer() из linux-main.cpp, C-версия).
// На H3 пишем в локальный RGB32-кадр PXT_W x PXT_H (640x480), потом скейлим.
// Кадр 1.2 МБ в общем .bss упёрся бы в CPS ZIP_BUF_BIG; кладём в пул аркад
// .noinit_fbn (0x54000000, единый владелец времени) как остальные pxt-буферы.
#define PXT_W 640
#define PXT_H 480
static uint32_t ALIGN(4, pxt_screen[PXT_W * PXT_H]) __attribute__((section(".noinit_fbn")));
uint8_t ALIGN(4, DEBUG_VRAM[80 * 10]) = {0};

// r1413: HDD-образ PXT (/roms/pxt/hdd/hdd.img, 256 МБ) не влезает в RAM-пул.
// Читаем с носителя (SD/USB-флешка) потоком: поток держит текущий кластер,
// как f_lseek+f_read в эталоне. Без него сектор из середины HDD тянул всю
// цепочку FAT от первого кластера — BIOS не успевал к тику (Divide overflow).
static fat_entry_t g_hdd[2];
static fat_stream_t g_hdd_stream[2];
static uint32_t g_hdd_size[2];

static int pxt_hdd_read_slot(int slot, uint32_t off, uint8_t* buf, uint32_t len) {
    if (slot < 0 || slot > 1 || !g_hdd_size[slot]) return -1;
    if (off + len > g_hdd_size[slot]) return -1;
    return (fat_stream_read(&g_hdd_stream[slot], off, buf, len) == (int)len) ? 0 : -1;
}

static int pxt_hdd_read0(uint32_t off, uint8_t* buf, uint32_t len) {
    return pxt_hdd_read_slot(0, off, buf, len);
}
static int pxt_hdd_read1(uint32_t off, uint8_t* buf, uint32_t len) {
    return pxt_hdd_read_slot(1, off, buf, len);
}

// r1413: монтирование HDD из папки рома (как на флешке): hdd.img -> C: (0x80),
// hdd2.img -> D: (0x81). Образы read-only и не копируются в RAM.
void pxt_mount_hdd(void) {
    static const char* const names[2] = { "hdd.img", "hdd2.img" };
    static const uint8_t units[2] = { 0x80, 0x81 };
    for (int i = 0; i < 2; i++) {
        fat_entry_t e;
        if (fat_find("/roms/pxt/hdd", names[i], &e) && e.size) {
            g_hdd[i] = e;
            g_hdd_size[i] = e.size;
            fat_stream_init(&g_hdd_stream[i], &g_hdd[i]);
            pxt_disk_set_stream(units[i], i ? pxt_hdd_read1 : pxt_hdd_read0, e.size);
            emulog_field("PXT", "disk", "HDD %c: /roms/pxt/hdd/%s, %u B (stream)",
                         i ? 'D' : 'C', names[i], (unsigned)e.size);
        }
    }
}

int cursor_blink_state = 0;
uint8_t log_debug = 0;

// пишем отладочный текст (как _putchar в linux-main) в DEBUG_VRAM.
void _putchar(char character) {
    static int x = 0, y = 0;
    if (y == 10) {
        y = 9;
        memmove(DEBUG_VRAM, DEBUG_VRAM + 80, 80 * 9);
        memset(DEBUG_VRAM + 80 * 9, 0, 80);
    }
    uint8_t *vidramptr = DEBUG_VRAM + y * 80 + x;
    if ((unsigned)character >= 32) {
        if (character >= 96) character -= 32;
        *vidramptr = (uint8_t)(((character - 32) & 63) | 0 << 6);
        if (x == 80) { x = 0; y++; } else x++;
    } else if (character == '\n') { x = 0; y++; }
    else if (character == '\r') { x = 0; }
    else if (character == 8 && x > 0) { x--; *vidramptr = 0; }
}

static void renderer(void) {
    uint32_t start_off = vram_offset;   // r1413: не мутируем CRTC start address
    uint8_t *vidramptr = (uint8_t *)VIDEORAM + 0x8000 + ((start_off & 0xffff) << 1);
    // r1413: VIDEORAM в pxt — uint32_t на адрес (4 плоскости). Текст BIOS
    // пишет через vga_mem_write: байт лежит в МЛАДШЕМ байте uint32, поэтому
    // читаем text_base[i] & 0xFF, а не байты подряд (раньше был чёрный экран).
    uint32_t *text_base = VIDEORAM + 0x8000 + ((start_off & 0xffff) << 1);
    uint8_t cols = 80;
    for (int y = 0; y < PXT_H; y++) {
        if (y >= 399) port3DA = 8; else port3DA = 0;
        if (y & 1) port3DA |= 1;
        uint32_t *pixels = pxt_screen + y * PXT_W;
        uint32_t *row_start = pixels;

        if (y < 400) {
            switch (videomode) {
                case 0x00:
                case 0x01: {
                    uint16_t y_div_16 = y / 16;
                    uint8_t glyph_line = (y / 2) % 8;
                    uint32_t *text_buffer_line = text_base + y_div_16 * 80;
                    for (int column = 0; column < 40; column++) {
                        uint8_t ch = (uint8_t)(text_buffer_line[column * 2] & 0xFF);
                        uint8_t color = (uint8_t)(text_buffer_line[column * 2 + 1] & 0xFF);
                        uint8_t glyph_pixels = font_8x8[ch * 8 + glyph_line];
                        uint8_t cursor_active = (uint8_t)(cursor_blink_state &&
                            y_div_16 == CURSOR_Y && column == CURSOR_X &&
                            glyph_line >= cursor_start && glyph_line <= cursor_end);
                        for (uint8_t bit = 0; bit < 8; bit++) {
                            uint8_t pixel_color;
                            if (cursor_active) pixel_color = color & 0x0F;
                            else if (cga_blinking && (color >> 7) & 1) pixel_color = cursor_blink_state ? (color >> 4) & 0x7 : color & 0x7;
                            else pixel_color = (glyph_pixels >> bit) & 1 ? color & 0x0f : color >> 4;
                            uint32_t pc = cga_palette[pixel_color];
                            pixels[0] = pixels[1] = pc;
                            pixels += 2;
                        }
                    }
                    break;
                }
                case 0x02:
                case 0x03:
                case 0x79: {
                    uint16_t y_div_16 = y / 16;
                    uint8_t glyph_line = y % 16;
                    uint32_t *text_row = text_base + y_div_16 * 160;
                    for (uint8_t column = 0; column < 80; column++) {
                        uint8_t ch = (uint8_t)(text_row[column * 2] & 0xFF);
                        uint8_t color = (uint8_t)(text_row[column * 2 + 1] & 0xFF);
                        uint8_t glyph_row = font_8x16[ch * 16 + glyph_line];
                        uint8_t cursor_active = (uint8_t)(cursor_blink_state &&
                            y_div_16 == CURSOR_Y && column == CURSOR_X &&
                            (cursor_start > cursor_end
                                 ? !(glyph_line >= cursor_end << 1 && glyph_line <= cursor_start << 1)
                                 : glyph_line >= cursor_start << 1 && glyph_line <= cursor_end << 1));
                        for (int bit = 0; bit < 8; bit++) {
                            uint8_t pixel_color;
                            if (cursor_active) pixel_color = color & 0x0F;
                            else if (cga_blinking && (color >> 7) & 1) {
                                if (cursor_blink_state) pixel_color = (color >> 4) & 0x7;
                                else pixel_color = (glyph_row >> bit) & 1 ? color & 0x0f : ((color >> 4) & 0x7);
                            } else pixel_color = (glyph_row >> bit) & 1 ? color & 0x0f : color >> 4;
                            *pixels++ = cga_palette[pixel_color];
                        }
                    }
                    break;
                }
                case 0x04:
                case 0x05: {
                    uint8_t *cga_row = vidramptr + ((y / 2 >> 1) * 80 + (y / 2 & 1) * 8192);
                    uint8_t *cpal = (uint8_t *)cga_gfxpal[cga_colorset][cga_intensity];
                    for (int x = 320 / 4; x--;) {
                        uint8_t b = *cga_row++;
                        uint32_t c0 = cga_palette[(b >> 6) & 3 ? cpal[(b >> 6) & 3] : cga_foreground_color];
                        uint32_t c1 = cga_palette[(b >> 4) & 3 ? cpal[(b >> 4) & 3] : cga_foreground_color];
                        uint32_t c2 = cga_palette[(b >> 2) & 3 ? cpal[(b >> 2) & 3] : cga_foreground_color];
                        uint32_t c3 = cga_palette[(b >> 0) & 3 ? cpal[(b >> 0) & 3] : cga_foreground_color];
                        pixels[0] = pixels[1] = c0; pixels += 2;
                        pixels[0] = pixels[1] = c1; pixels += 2;
                        pixels[0] = pixels[1] = c2; pixels += 2;
                        pixels[0] = pixels[1] = c3; pixels += 2;
                    }
                    break;
                }
                case 0x06: {
                    uint8_t *cga_row = vidramptr + (y / 2 >> 1) * 80 + (y / 2 & 1) * 8192;
                    for (int x = 640 / 8; x--;) {
                        uint8_t b = *cga_row++;
                        *pixels++ = cga_palette[(b >> 7 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 6 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 5 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 4 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 3 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 2 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 1 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(b >> 0 & 1) * cga_foreground_color];
                    }
                    break;
                }
                case 0x1e: cols = 90; start_off = 5; if (y >= 348) break;
                case 0x07: {
                    uint8_t *cga_row = (uint8_t *)((uint8_t *)VIDEORAM + start_off) + (y & 3) * 8192 + y / 4 * cols;
                    for (int x = 640 / 8; x--;) {
                        uint8_t b = *cga_row++;
                        *pixels++ = cga_palette[(b >> 7 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 6 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 5 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 4 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 3 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 2 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 1 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 0 & 1) * 15];
                    }
                    break;
                }
                case 0x08:
                case 0x74:
                case 0x76: {
                    uint32_t *palette;
                    switch (videomode) {
                        case 0x08: palette = tga_palette; break;
                        case 0x74: palette = cga_composite_palette[cga_intensity << 1]; break;
                        default:   palette = cga_composite_palette[0]; break;
                    }
                    uint8_t *cga_row = (uint8_t *)((uint8_t *)VIDEORAM + tga_offset) + (y / 2 >> 1) * 80 + (y / 2 & 1) * 8192;
                    for (int x = 640 / 8; x--;) {
                        uint8_t b = *cga_row++;
                        uint8_t c1 = b >> 4 & 15, c2 = b & 15;
                        if (!c1 && videomode == 0x8) c1 = cga_foreground_color;
                        if (!c2 && videomode == 0x8) c2 = cga_foreground_color;
                        uint32_t c1v = palette[c1];
                        uint32_t c2v = palette[c2];
                        pixels[0] = pixels[1] = pixels[2] = pixels[3] = c1v; pixels += 4;
                        pixels[0] = pixels[1] = pixels[2] = pixels[3] = c2v; pixels += 4;
                    }
                    break;
                }
                case 0x09: {
                    uint8_t *tga_row = (uint8_t *)((uint8_t *)VIDEORAM + tga_offset) + (y / 2 & 3) * 8192 + y / 8 * 160;
                    for (int x = 320 / 2; x--;) {
                        uint8_t b = *tga_row++;
                        uint32_t c1 = tga_palette[tga_palette_map[b >> 4 & 15]];
                        uint32_t c2 = tga_palette[tga_palette_map[b & 15]];
                        pixels[0] = pixels[1] = c1; pixels += 2;
                        pixels[0] = pixels[1] = c2; pixels += 2;
                    }
                    break;
                }
                case 0x0a: {
                    uint8_t *tga_row = (uint8_t *)VIDEORAM + y / 2 * 320;
                    for (int x = 640 / 2; x--;) {
                        uint8_t b = *tga_row++;
                        *pixels++ = tga_palette[tga_palette_map[b >> 4 & 15]];
                        *pixels++ = tga_palette[tga_palette_map[b & 15]];
                    }
                    break;
                }
                case 0x0D:
                case 0x0E: {
                    if (y >= 400) break;
                    uint32_t *vram_ptr = &VIDEORAM[(y / 2) * (videomode == 0x0D ? (320 / 8) : (640 / 8))];
                    for (int i = 0; i < (videomode == 0x0D ? (320 / 8) : (640 / 8)); ++i) {
                        uint32_t eight = vram_ptr[i];
                        uint8_t p0 = eight & 0xFF, p1 = (eight >> 8) & 0xFF, p2 = (eight >> 16) & 0xFF, p3 = (eight >> 24) & 0xFF;
                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t ci = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
                            uint32_t color = vga_palette[ci];
                            *pixels++ = color; if (videomode == 0x0D) *pixels++ = color;
                        }
                    }
                    break;
                }
                case 0x10: {
                    if (y >= 350) break;
                    uint32_t *vram_ptr = &VIDEORAM[y * (640 / 8)];
                    for (int i = 0; i < (640 / 8); ++i) {
                        uint32_t eight = vram_ptr[i];
                        uint8_t p0 = eight & 0xFF, p1 = (eight >> 8) & 0xFF, p2 = (eight >> 16) & 0xFF, p3 = (eight >> 24) & 0xFF;
                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t ci = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
                            *pixels++ = vga_palette[ci];
                        }
                    }
                    break;
                }
                case 0x11: {
                    uint8_t *cga_row = (uint8_t *)(VIDEORAM + y * 80);
                    for (int x = 640 / 8; x--;) {
                        uint8_t b = *cga_row++;
                        *pixels++ = cga_palette[(b >> 7 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 6 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 5 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 4 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 3 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 2 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 1 & 1) * 15];
                        *pixels++ = cga_palette[(b >> 0 & 1) * 15];
                    }
                    break;
                }
                case 0x12: {
                    if (y >= 480) break;
                    uint32_t *vram_ptr = &VIDEORAM[y * (640 / 8)];
                    for (int i = 0; i < (640 / 8); ++i) {
                        uint32_t eight = vram_ptr[i];
                        uint8_t p0 = eight & 0xFF, p1 = (eight >> 8) & 0xFF, p2 = (eight >> 16) & 0xFF, p3 = (eight >> 24) & 0xFF;
                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t ci = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
                            *pixels++ = vga_palette[ci];
                        }
                    }
                    break;
                }
                case 0x13: {
                    if (vga_planar_mode) {
                        for (int x = 0; x < 320; x++) {
                            uint32_t ptr = x + (y >> 1) * 320;
                            ptr = (ptr >> 2) + (x & 3) * vga_plane_size;
                            ptr += vram_offset;
                            uint32_t color = vga_palette[VIDEORAM[ptr]];
                            pixels[0] = pixels[1] = color;
                            pixels += 2;
                        }
                    } else {
                        uint8_t *vga_row = (uint8_t *)VIDEORAM + (y >> 1) * 320;
                        for (int x = 0; x < 320; x++) {
                            uint32_t color = vga_palette[*vga_row++];
                            pixels[0] = pixels[1] = color;
                            pixels += 2;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            // r1413: режимы, которые не покрывают кадр целиком (0x10 — 350
            // строк, 0x1e — 348, неизвестный default), добиваем строку фоном,
            // иначе справа/снизу остаётся прошлый кадр или мусор .noinit_fbn.
            while (pixels < row_start + PXT_W) *pixels++ = cga_palette[0];
        } else {
            // r1424: debug band убран с экрана — диагностика только в UART.
            for (int x = 0; x < PXT_W; x++) *pixels++ = cga_palette[0];
        }
    }
}

// ---------------------------------------------------------------------------
// Ввод: HID -> PC set-1 сканкод (как у MS1504 host / HandleInput linux).
// ---------------------------------------------------------------------------
static int pxt_hid_to_pc(uint8_t h) {
    switch (h) {
    case 0x04: return 0x1E; case 0x05: return 0x30; case 0x06: return 0x2E;
    case 0x07: return 0x20; case 0x08: return 0x12; case 0x09: return 0x21;
    case 0x0A: return 0x22; case 0x0B: return 0x23; case 0x0C: return 0x17;
    case 0x0D: return 0x24; case 0x0E: return 0x25; case 0x0F: return 0x26;
    case 0x10: return 0x32; case 0x11: return 0x31; case 0x12: return 0x18;
    case 0x13: return 0x19; case 0x14: return 0x10; case 0x15: return 0x13;
    case 0x16: return 0x1F; case 0x17: return 0x14; case 0x18: return 0x16;
    case 0x19: return 0x2F; case 0x1A: return 0x11; case 0x1B: return 0x2D;
    case 0x1C: return 0x15; case 0x1D: return 0x2C;
    case 0x1E: return 0x02; case 0x1F: return 0x03; case 0x20: return 0x04;
    case 0x21: return 0x05; case 0x22: return 0x06; case 0x23: return 0x07;
    case 0x24: return 0x08; case 0x25: return 0x09; case 0x26: return 0x0A;
    case 0x27: return 0x0B;
    case 0x28: return 0x1C; case 0x29: return 0x01; case 0x2A: return 0x0E;
    case 0x2B: return 0x0F; case 0x2C: return 0x39; case 0x2D: return 0x0C;
    case 0x2E: return 0x0D; case 0x2F: return 0x1A; case 0x30: return 0x1B;
    case 0x31: return 0x2B; case 0x33: return 0x27; case 0x34: return 0x28;
    case 0x35: return 0x29; case 0x36: return 0x33; case 0x37: return 0x34;
    case 0x38: return 0x35; case 0x39: return 0x3A;
    case 0x3A: return 0x3B; case 0x3B: return 0x3C; case 0x3C: return 0x3D;
    case 0x3D: return 0x3E; case 0x3E: return 0x3F; case 0x3F: return 0x40;
    case 0x40: return 0x41; case 0x41: return 0x42; case 0x42: return 0x43;
    case 0x43: return 0x44; case 0x44: return 0x57; case 0x45: return 0x58;
    case 0x46: return 0x37; case 0x47: return 0x46; case 0x49: return 0x52;
    case 0x4A: return 0x47; case 0x4B: return 0x49; case 0x4C: return 0x53;
    case 0x4D: return 0x4F; case 0x4E: return 0x51; case 0x4F: return 0x4D;
    case 0x50: return 0x4B; case 0x51: return 0x50; case 0x52: return 0x48;
    case 0x54: return 0x35; case 0x55: return 0x37; case 0x56: return 0x4A;
    case 0x57: return 0x4E; case 0x58: return 0x1C;
    case 0x59: return 0x4F; case 0x5A: return 0x50; case 0x5B: return 0x51;
    case 0x5C: return 0x4B; case 0x5D: return 0x4C; case 0x5E: return 0x4D;
    case 0x5F: return 0x47; case 0x60: return 0x48; case 0x61: return 0x49;
    case 0x62: return 0x52; case 0x63: return 0x53;
    case 0xE0: case 0xE4: return 0x1D; case 0xE1: return 0x2A; case 0xE5: return 0x36;
    case 0xE2: case 0xE6: return 0x38;
    default: return -1;
    }
}

static uint8_t g_kbd_prev[256];
static uint8_t g_mod_prev;   // r1413: сбрасывается при входе (не наследуем зажатый мод)
// r1413: автоповтор удержанной клавиши — BIOS/8042 в эмуляции его не делают.
#define PXT_REPEAT_DELAY_US 350000u
#define PXT_REPEAT_RATE_US   90000u
static uint32_t g_kbd_down_t[256];
static uint32_t g_kbd_rep_t[256];
static void pxt_kbd_update(void) {
    uint8_t raw[8];
    int n = usb_kbd_get_raw(raw, 8);
    // модификаторы сначала
    {
        uint8_t mk = usb_kbd_get_mods();
        static const uint8_t mod_hid[6] = {0xE0, 0xE1, 0xE2, 0xE4, 0xE5, 0xE6};
        static const uint8_t mod_bit[6] = {0x01, 0x02, 0x04, 0x10, 0x20, 0x40};
        for (int m = 0; m < 6; m++) {
            int on = (mk & mod_bit[m]) != 0, was = (g_mod_prev & mod_bit[m]) != 0;
            if (on != was) {
                int pc = pxt_hid_to_pc(mod_hid[m]);
                if (pc >= 0) { g_kbd_prev[pc] = (uint8_t)(on ? 1 : 0); keyboard_push(on ? (uint8_t)pc : (uint8_t)(pc | 0x80)); }
            }
        }
        g_mod_prev = mk;
    }
    for (int i = 0; i < n; i++) {
        int pc = pxt_hid_to_pc(raw[i]);
        if (pc < 0) continue;
        if (!g_kbd_prev[pc]) { g_kbd_prev[pc] = 1; keyboard_push((uint8_t)pc); }
    }
    // r1413: повтор зажатой клавиши по кадровому таймеру (пороги как в меню).
    {
        uint32_t now = h3_hs_timer_lo_us();
        for (int i = 0; i < n; i++) {
            int pc = pxt_hid_to_pc(raw[i]);
            if (pc < 0 || !g_kbd_prev[pc]) continue;
            if (!g_kbd_down_t[pc]) { g_kbd_down_t[pc] = now; g_kbd_rep_t[pc] = now; continue; }
            if (now - g_kbd_down_t[pc] >= PXT_REPEAT_DELAY_US &&
                now - g_kbd_rep_t[pc] >= PXT_REPEAT_RATE_US) {
                g_kbd_rep_t[pc] = now;
                keyboard_push((uint8_t)pc);
            }
        }
    }
    for (int pc2 = 1; pc2 < 0x59; pc2++) {
        // r1431: модификаторы (Ctrl/LShift/RShift/Alt) ведёт блок выше.
        // В raw[] их нет, и общий цикл слал break в том же кадре —
        // Shift нажимался и сразу отпускался (DOS не видел спецсимволов).
        if (pc2 == 0x1D || pc2 == 0x2A || pc2 == 0x36 || pc2 == 0x38) continue;
        if (g_kbd_prev[pc2]) {
            int still = 0;
            for (int i = 0; i < n; i++)
                if (pxt_hid_to_pc(raw[i]) == pc2) { still = 1; break; }
            if (!still) {
                g_kbd_prev[pc2] = 0;
                g_kbd_down_t[pc2] = 0;
                g_kbd_rep_t[pc2] = 0;
                keyboard_push((uint8_t)(pc2 | 0x80));
            }
        }
    }
}

// r1432: тачпад клавиатуры (Avatto 603 — HID boot mouse). Без usb_pad_poll()
// дельты не накапливались вовсе: Amiga (uae4all_host.c:231) опрашивает его
// каждый кадр, PXT — нет. Кнопки без движения тоже отправляем (DOS-мышь).
static uint8_t g_mouse_btn_prev;
static void pxt_mouse_update(void) {
    usb_pad_poll();
    int dx = 0, dy = 0;
    usb_mouse_take_delta(&dx, &dy);
    const uint8_t btns = (uint8_t)(usb_pad_buttons() & 7);
    if (!dx && !dy && btns == g_mouse_btn_prev) return;
    if (dx || dy) {
        while (dx || dy) {
            int8_t sx = (int8_t)(dx > 127 ? 127 : (dx < -128 ? -128 : dx));
            int8_t sy = (int8_t)(dy > 127 ? 127 : (dy < -128 ? -128 : dy));
            sermouseevent(btns, sx, sy);
            dx -= sx;
            dy -= sy;
        }
    } else {
        sermouseevent(btns, 0, 0);
    }
    g_mouse_btn_prev = btns;
}

// ---------------------------------------------------------------------------
// Аудио: get_sound_sample (стерео 44100) -> rs44fs_stereo -> i2s.
// ---------------------------------------------------------------------------
static void pxt_audio_flush(void) {
    extern void get_sound_sample(int16_t other, int16_t *samples);
    static int16_t snd_buf[2048 * 2];
    // r1413: DMA SB двигаем по фактическому времени кадра (на H3 нет второго
    // ядра, которое звало blaster_tick); скачок после простоя клампим кадром.
    static uint32_t last_us;
    uint32_t now = h3_hs_timer_lo_us();
    uint32_t dt = last_us ? (now - last_us) : (1000000u / 60u);
    last_us = now;
    if (dt > 100000u) dt = 1000000u / 60u;
    blaster_advance(dt);
    int frames = SOUND_FREQUENCY / 60;   // ~735 на кадр 60 Гц
    if (frames > 2048) frames = 2048;    // не более выходного rs-буфера
    for (int i = 0; i < frames; i++)
        get_sound_sample(0, &snd_buf[i * 2]);
    static uint32_t rs_phase = 0;
    static int16_t rs_hist[RS44_HIST * 2] = {0};
    static int16_t out2048[2048 * 2];
    uint32_t o = rs44fs_stereo(&rs_phase, rs_hist, snd_buf, (uint32_t)frames, out2048, 2048);
    if (o) i2s_push_block(out2048, o);
}

// ---------------------------------------------------------------------------
// init + run
// ---------------------------------------------------------------------------
// Диски: настраиваются извне (rom_browser) через pxt_disk_set (disks-h3).
// Здесь наполняем BDA (как fake86/MS1504 готовит boot-флоппи).

extern void blaster_reset(void);
extern void sn76489_reset(void);
extern void pxt_disk_set(uint8_t drivenum, const uint8_t *base, size_t len);
extern void pxt_disk_clear(uint8_t drivenum);

// точка входа из rom_browser: rom указывает на загрузочный образ (флоппи или
// HDD) в аркадном пуле, size — его размер. Конфигурацию дисков host задаёт
// ДО (см. rom_browser ветку pxt), здесь при необходимости монтируем.
void emu_run_pxt(const uint8_t *rom, uint32_t size, const char *rom_name) {
    emu_prepare();
    emulog_begin("PXT");
    emulog_field("PXT", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    snd_manifest("pxt", "spkr;cms;sndblst;sn76489");

    // стерео 44100 на H3
    // назначить функции памяти на onboard (DRAM)
    write86 = write86_ob;
    writew86 = writew86_ob;
    writedw86 = writedw86_ob;
    read86 = read86_ob;
    readw86 = readw86_ob;
    readdw86 = readdw86_ob;

    // сбросить слои ядра полностью (r1413: повторный вход не наследует PIT/PIC/
    // видеорежим прошлой сессии; .noinit_fbn сам по себе не обнуляется)
    memset(RAM, 0, sizeof(RAM));
    memset(UMB, 0, sizeof(UMB));
    memset(HMA, 0, sizeof(HMA));
    ems_reset();   // r1439: EMS в .noinit_fbn не обнуляется сам
    memset(VIDEORAM, 0, sizeof(VIDEORAM));
    memset(pxt_screen, 0, sizeof(pxt_screen));
    memset(crt_controller, 0, sizeof(crt_controller));
    i8259_reset();
    init8253();
    videomode = 0x03;
    vram_offset = 0;
    tga_offset = 0x8000;
    cursor_start = 12;
    cursor_end = 13;
    g_mod_prev = 0;
    g_mouse_btn_prev = 0;
    blaster_reset();
    sn76489_reset();

    // если rom — это флоппи-образ (обычно), вставить как A: (drive 0).
    // Индиск C:/D: (HDD) задаётся rom_browser заранее через pxt_disk_set.
    if (rom && size) {
        // drive 0 = A:
        pxt_disk_clear(0);
        pxt_disk_set(0, rom, size);
        emulog_field("PXT", "disk", "A: %u B", (unsigned)size);
    }

    reset86();
    // после reset86 BDA инициализирована; warm-boot маркер и boot-привод
    RAM[0x472] = 0x34; RAM[0x473] = 0x12;   // warm boot (как у MS1504)
    emulog_field("PXT", "cpu", "reset, boot");

    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));
    memset(g_kbd_down_t, 0, sizeof(g_kbd_down_t));
    memset(g_kbd_rep_t, 0, sizeof(g_kbd_rep_t));
    emulog_field("PXT", "video", "640x480");
    emulog_field("PXT", "machine", "8086/286");
    emulog_field("PXT", "init", "OK");
    // r1429: бордюр маджента (0x00FF00FF) — визуальная проверка на стенде:
    // видно границы кадра 640x480 и полей HDMI, не путая с чёрным растром DOS.
    emu_set_border_color(0x00FF00FF);
    emu_throttle_reset();
    emu_esc_hold_reset();

    // r1440: квант 32768 (как linux-main) на H3 давал опаздывающий IRQ0: пока
    // CPU0 ждёт носитель (чтение HDD/FAT), цикл не проверяет тик, DOS/LTEMM
    // видит «замершее» время и делит на него (Divide overflow). 4096 — тик
    // проверяется в 8 раз чаще, задержка меньше миллисекунды.
    uint32_t tormoz = 4096;
    int dbg_frames = 0;
    int boot_frames = 0, boot_pause_done = 0;
    uint32_t pit_last = h3_hs_timer_lo_us();
    int pit_started = 0;
    for (;;) {
        pxt_kbd_update();
        pxt_mouse_update();
        // r1421: IRQ0 включаем только после того, как BIOS инициализировал PIC
        // (маска перестала быть 0xFF). Раньше тик шёл с первого кадра — до
        // заполнения IVT: IRQ0 уводил CPU по мусорному вектору (0:00DF),
        // отсюда «DIVIDE overflow» и BDA=63/63.
        uint32_t now_us = h3_hs_timer_lo_us();
        if (!pit_started) {
            // r1423: если BIOS не размаскировал PIC (некоторые образы/драйверы
            // сами включают таймер позже), всё равно запускаем IRQ0 после ~1 с:
            // без тика DOS/EMS-драйверы делят на нулевой счётчик.
            if (i8259.interrupt_mask_register != 0xFF || dbg_frames > 60) {
                pit_started = 1;
                pit_last = now_us;
            }
        } else if ((uint32_t)(now_us - pit_last) >= 54925u) {
            pit_last += 54925u;
            doirq(0);
        }
        exec86(tormoz);
        renderer();
        // r1413: одноразовая диагностика «чёрного экрана» — виден ли текст BIOS
        // в VIDEORAM, заполняется ли кадр и где стоит CPU.
        if (++dbg_frames == 300)   // r1418: ~5 с — BIOS успевает вывести текст
            // r1418: через emulog (h3 printf → UART). Прямой printf в pxt_host
            // перехвачен printf.h (`#define printf printf_`) и уходит в
            // DEBUG_VRAM на экран, а не в UART.
            emulog_field("PXT", "dbg",
                         "mode=%x vram=%02x scr=%x bda=%x/%x a20=%d hma=%x ram=%x imr=%02x pit=%d cs:ip=%04x:%04x",
                         videomode, (unsigned)(VIDEORAM[0x8000] & 0xFF),
                         (unsigned)pxt_screen[0], (unsigned)RAM[0x410],
                         (unsigned)RAM[0x449], a20_enabled,
                         (unsigned)HMA[0xF440], (unsigned)RAM[0xF440],
                         (unsigned)i8259.interrupt_mask_register, pit_started,
                         (unsigned)(segregs32[2] & 0xFFFF), (unsigned)(ip32 & 0xFFFF));
        // r1413: кадр 640x480 RGB32 идёт в HDMI напрямую (4:3, поля по бокам),
        // без промежуточного 320x240 RGB565 — нет двойного nearest-размытия.
        emu_show_rgba_int((const uint32_t *)pxt_screen, PXT_W, PXT_H, PXT_W);
        fb_flush();
        // r1433/r1435: POST Turbo XT BIOS проходит за 1-2 кадра и тут же
        // стирается INT 10h перед загрузкой. Держим 1 с тот кадр, где в
        // текстовой видеопамяти уже появился символ (не пробел), а не
        // фиксированный 5-й кадр: к нему BIOS уже успевал очистить экран.
        if (!boot_pause_done) {
            int has_text = 0;
            for (int i = 0; i < 256; i++)
                if ((VIDEORAM[0x8000 + i] & 0xFF) != 0x20 &&
                    (VIDEORAM[0x8000 + i] & 0xFF) != 0) { has_text = 1; break; }
            if (has_text || ++boot_frames >= 30) {
                udelay(1000000);
                boot_pause_done = 1;
            }
        }
        pxt_audio_flush();
        emu_throttle();
        if (emu_esc_hold()) break;
    }

    emu_audio_stop();
    fb_clear();
    fb_flush();
    emulog_end("PXT");
}