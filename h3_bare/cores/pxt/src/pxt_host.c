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

extern int printf(const char *fmt, ...);

#define EMU_FB      ((volatile uint16_t *)0x5F800000u)
#define EMU_FB_W    320
#define EMU_FB_H    240

// ---------------------------------------------------------------------------
// Рендер (перенос renderer() из linux-main.cpp, C-версия).
// На H3 пишем в локальный RGB32-кадр PXT_W x PXT_H (640x480), потом скейлим.
// Кадр 1.2 МБ в общем .bss упёрся бы в CPS ZIP_BUF_BIG; кладём в пул аркад
// .noinit_fbn (0x54000000, единый владелец времени) как остальные pxt-буферы.
#define PXT_W 640
#define PXT_H 480
static uint32_t ALIGN(4, pxt_screen[PXT_W * PXT_H]) __attribute__((section(".noinit_fbn")));
uint8_t ALIGN(4, DEBUG_VRAM[80 * 10]) = {0};

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
    uint8_t *vidramptr = (uint8_t *)VIDEORAM + 0x8000 + ((vram_offset & 0xffff) << 1);
    uint8_t cols = 80;
    for (int y = 0; y < PXT_H; y++) {
        if (y >= 399) port3DA = 8; else port3DA = 0;
        if (y & 1) port3DA |= 1;
        uint32_t *pixels = pxt_screen + y * PXT_W;

        if (y < 400) {
            switch (videomode) {
                case 0x00:
                case 0x01: {
                    uint16_t y_div_16 = y / 16;
                    uint8_t glyph_line = (y / 2) % 8;
                    uint8_t *text_buffer_line = vidramptr + y_div_16 * 80;
                    for (int column = 0; column < 40; column++) {
                        uint8_t glyph_pixels = font_8x8[*text_buffer_line++ * 8 + glyph_line];
                        uint8_t color = *text_buffer_line++;
                        uint8_t cursor_active = (uint8_t)(cursor_blink_state &&
                            y_div_16 == CURSOR_Y && column == CURSOR_X &&
                            glyph_line >= cursor_start && glyph_line <= cursor_end);
                        for (uint8_t bit = 0; bit < 8; bit++) {
                            uint8_t pixel_color;
                            if (cursor_active) pixel_color = color & 0x0F;
                            else if (cga_blinking && (color >> 7) & 1) pixel_color = cursor_blink_state ? (color >> 4) & 0x7 : color & 0x7;
                            else pixel_color = (glyph_pixels >> bit) & 1 ? color & 0x0f : color >> 4;
                            *pixels++ = *pixels++ = cga_palette[pixel_color];
                        }
                    }
                    break;
                }
                case 0x02:
                case 0x03: {
                    uint16_t y_div_16 = y / 16;
                    uint8_t glyph_line = y % 16;
                    uint8_t *text_row = vidramptr + y_div_16 * 160;
                    for (uint8_t column = 0; column < 80; column++) {
                        uint8_t *charcode = text_row + column * 2;
                        uint8_t glyph_row = font_8x16[*charcode * 16 + glyph_line];
                        uint8_t color = *++charcode;
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
                        *pixels++ = *pixels++ = cga_palette[(b >> 6) & 3 ? cpal[(b >> 6) & 3] : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[(b >> 4) & 3 ? cpal[(b >> 4) & 3] : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[(b >> 2) & 3 ? cpal[(b >> 2) & 3] : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[(b >> 0) & 3 ? cpal[(b >> 0) & 3] : cga_foreground_color];
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
                case 0x1e: cols = 90; vram_offset = 5; if (y >= 348) break;
                case 0x07: {
                    uint8_t *cga_row = (uint8_t *)((uint8_t *)VIDEORAM + vram_offset) + (y & 3) * 8192 + y / 4 * cols;
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
                        *pixels++ = *pixels++ = *pixels++ = *pixels++ = palette[c1];
                        *pixels++ = *pixels++ = *pixels++ = *pixels++ = palette[c2];
                    }
                    break;
                }
                case 0x09: {
                    uint8_t *tga_row = (uint8_t *)((uint8_t *)VIDEORAM + tga_offset) + (y / 2 & 3) * 8192 + y / 8 * 160;
                    for (int x = 320 / 2; x--;) {
                        uint8_t b = *tga_row++;
                        *pixels++ = *pixels++ = tga_palette[tga_palette_map[b >> 4 & 15]];
                        *pixels++ = *pixels++ = tga_palette[tga_palette_map[b & 15]];
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
                            *pixels++ = *pixels++ = color;
                        }
                    } else {
                        uint8_t *vga_row = (uint8_t *)VIDEORAM + (y >> 1) * 320;
                        for (int x = 0; x < 320; x++) {
                            uint32_t color = vga_palette[*vga_row++];
                            *pixels++ = *pixels++ = color;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        } else {
            // debug band (y >= 400), как в linux-main: 80x10 текст
            uint8_t ydebug = (uint8_t)(y - 400);
            uint8_t y_div_8 = ydebug / 8, glyph_line = ydebug % 8;
            const uint8_t colors[4] = {0x0f, 0xf0, 10, 12};
            uint8_t *text_buffer_line = &DEBUG_VRAM[y_div_8 * 80];
            for (uint8_t column = 80; column--;) {
                uint8_t character = *text_buffer_line++;
                uint8_t color = colors[character >> 6];
                uint8_t glyph_pixels = font_8x8[(32 + (character & 63)) * 8 + glyph_line];
                for (int bit = 0; bit < 8; bit++)
                    *pixels++ = cga_palette[(glyph_pixels >> bit) & 1 ? color & 0x0f : color >> 4];
            }
        }
    }
}

// масштабирование 640x480 RGB32 -> EMU_FB 320x240 RGB565 (1:2 downscale, но
// жмём каждую 2x2 группу в один пиксель — асимметричный, наиболее встречаемые
// текстовые/EGA-режимы 320x200/640x400).
static uint16_t rgb32to565(uint32_t c) {
    uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void pxt_scale_to_emufb(void) {
    volatile uint16_t *dst = EMU_FB;
    for (int y = 0; y < EMU_FB_H; y++) {
        const uint32_t *src = pxt_screen + (y * PXT_H / EMU_FB_H) * PXT_W;
        for (int x = 0; x < EMU_FB_W; x++)
            dst[y * EMU_FB_W + x] = rgb32to565(src[x * PXT_W / EMU_FB_W]);
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
static void pxt_kbd_update(void) {
    uint8_t raw[8];
    int n = usb_kbd_get_raw(raw, 8);
    // модификаторы сначала
    {
        static uint8_t g_mod_prev = 0;
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
    for (int pc2 = 1; pc2 < 0x59; pc2++)
        if (g_kbd_prev[pc2]) {
            int still = 0;
            for (int i = 0; i < n; i++)
                if (pxt_hid_to_pc(raw[i]) == pc2) { still = 1; break; }
            if (!still) { g_kbd_prev[pc2] = 0; keyboard_push((uint8_t)(pc2 | 0x80)); }
        }
}

// ---------------------------------------------------------------------------
// Аудио: get_sound_sample (стерео 44100) -> rs44fs_stereo -> i2s.
// ---------------------------------------------------------------------------
static void pxt_audio_flush(void) {
    extern void get_sound_sample(int16_t other, int16_t *samples);
    static int16_t snd_buf[2048 * 2];
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

    // сбросить слои ядра полностью
    memset(RAM, 0, sizeof(RAM));
    memset(UMB, 0, sizeof(UMB));
    memset(HMA, 0, sizeof(HMA));
    memset(VIDEORAM, 0, sizeof(VIDEORAM));
    blaster_reset();
    sn76489_reset();

    // если rom — это флоппи-образ (обычно), вставить как A: (drive 0).
    // Индиск C:/D: (HDD) задаётся rom_browser заранее через pxt_disk_set.
    if (rom && size) {
        // drive 0 = A:
        pxt_disk_clear(0);
        pxt_disk_set(0, rom, size);
        printf("PXT: boot disk A: %u Б\n", (unsigned)size);
    }

    reset86();
    // после reset86 BDA инициализирована; warm-boot маркер и boot-привод
    RAM[0x472] = 0x34; RAM[0x473] = 0x12;   // warm boot (как у MS1504)
    printf("PXT: cpu reset, boot\n");

    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));
    emulog_field("PXT", "video", "640x480");
    emulog_field("PXT", "machine", "8086/286");
    emulog_field("PXT", "init", "OK");
    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();

    uint32_t tormoz = 32768;  // как linux-main
    for (;;) {
        pxt_kbd_update();
        exec86(tormoz);
        renderer();
        pxt_scale_to_emufb();
        emu_scale_rgb565_src((const uint16_t *)(uintptr_t)EMU_FB, EMU_FB_W, EMU_FB_H, EMU_FB_W);
        pxt_audio_flush();
        emu_throttle();
        if (emu_esc_hold()) break;
    }

    emu_audio_stop();
    fb_clear();
    fb_flush();
    emulog_end("PXT");
}