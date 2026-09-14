/* System layer for the Atari 7800 core (ProSystem) on pico-retro.
 * Provides: shared memory pool (reusing NES buffers), cartridge load from
 * flash (XIP), line renderer to the ILI9341 and joypad input.
 *
 * Port of the GPL ProSystem emulator (Greg Stanton), adapted for RP2040:
 * - memory_ram (16K, 0x0000..0x3FFF) is the writable RAM, taken from the
 *   shared NES-buffer pool.
 * - The 64K memory_rom flag array became an 8-slot map (see Memory.cpp), and
 *   cartridge ROM is read straight from flash — no copy in RAM.
 * - maria_surface (91KB) is gone: maria_LineReady() pushes each rendered line
 *   straight to the LCD.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "display.h"
#include "joypad.h"
}

/* ProSystem core is C++, not extern "C" like the older cores. */
#include "ProSystem.h"
#include "Maria.h"
#include "Memory.h"
#include "Cartridge.h"
#include "Region.h"

/* ------------------------------------------------------------------ */
/* Static memory pool — reuses NES-only buffers (7800 and NES never run */
/* together). The core needs 64K RAM; we take it from the pool.        */
/* ------------------------------------------------------------------ */
extern "C" uint8_t SCREEN[240][256];
extern "C" uint8_t ChrBuf[];
extern "C" uint8_t RAM[];
extern "C" uint8_t SRAM[];

static struct {
    uint8_t *base;
    int      size;
    int      used;
} a7_pool[] = {
    { (uint8_t *)&SCREEN[0][0], 240 * 256, 0 },
    { ChrBuf,                   256 * 2 * 8 * 8, 0 },
    { RAM,                      0x2000, 0 },
    { SRAM,                     0x2000, 0 },
};
#define A7_POOL_REGS (sizeof(a7_pool) / sizeof(a7_pool[0]))

static void a7_pool_init(void)
{
    for (unsigned i = 0; i < A7_POOL_REGS; i++) a7_pool[i].used = 0;
}

static void *a7_malloc(int size)
{
    for (unsigned i = 0; i < A7_POOL_REGS; i++) {
        if (a7_pool[i].used + size <= a7_pool[i].size) {
            void *p = a7_pool[i].base + a7_pool[i].used;
            a7_pool[i].used += size;
            return p;
        }
    }
    printf("[a7800] pool exhausted for %d bytes\n", size);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Renderer: 256-colour LUT (palette_data -> RGB565) + line blit.      */
/* ------------------------------------------------------------------ */
static uint16_t a7_pal_rgb565[256];
static uint16_t a7_line_rgb[320];

static void a7_build_palette(void)
{
    extern byte palette_data[768];
    for (int i = 0; i < 256; i++) {
        byte r = palette_data[i * 3 + 0];
        byte g = palette_data[i * 3 + 1];
        byte b = palette_data[i * 3 + 2];
        a7_pal_rgb565[i] = RGB565(r >> 3, g >> 2, b >> 3);
    }
}

/* Called by maria_RenderScanline() once per line in the visible area
 * (26..248 NTSC, 26..297 PAL) with 320 palette indices. The display window is
 * opened once per frame in a7800_run_frame(), so here we only push pixels.
 * The whole visible area is scaled evenly onto the 240 display rows (1.08x for
 * NTSC, 0.88x for PAL), so every LCD row is written every frame — no leftover
 * menu pixels at the bottom and no wild stretching when a game only draws in
 * the upper part of the screen. */
void maria_LineReady(const byte *line, int length)
{
    if (length > 320) length = 320;
    for (int x = 0; x < length; x++)
        a7_line_rgb[x] = a7_pal_rgb565[line[x] & 0xFF];

    int top = (int)maria_visibleArea.top;
    int h   = (int)maria_visibleArea.bottom - top + 1;
    int sy  = (int)maria_scanline - top;
    if (sy < 0 || h <= 0) return;
    int y0 = (sy * 240) / h;
    int y1 = ((sy + 1) * 240) / h;
    if (y0 >= 240) return;
    if (y1 > 240) y1 = 240;
    for (int y = y0; y < y1; y++)
        display_stream_pixels16(a7_line_rgb, length, 1);
}

/* ------------------------------------------------------------------ */
/* Input: joypad to the 17-byte RIOT control array.                    */
/* ------------------------------------------------------------------ */
static void a7_build_input(byte *input)
{
    uint8_t pad = joypad_buttons();   /* 0 = pressed, NES bit order */
    /* Joystick 1 (offset 0..5): Right, Left, Down, Up, B1, B2 */
    input[0] = (pad & 0x80) ? 0 : 1;  /* Right  */
    input[1] = (pad & 0x40) ? 0 : 1;  /* Left   */
    input[2] = (pad & 0x20) ? 0 : 1;  /* Down   */
    input[3] = (pad & 0x10) ? 0 : 1;  /* Up     */
    input[4] = (pad & 0x01) ? 0 : 1;  /* B1 (A) */
    input[5] = (pad & 0x02) ? 0 : 1;  /* B2 (B) */
    /* Joystick 2 idle (offsets 6..11) */
    for (int i = 6; i < 12; i++) input[i] = 0;
    /* Console: Reset(12), Select(13), Pause(14), LeftDiff(15), RightDiff(16) */
    input[12] = 0;
    input[13] = (pad & 0x04) ? 0 : 1; /* Select */
    input[14] = 0;                    /* Pause  */
    input[15] = 0;
    input[16] = 0;
}

/* ------------------------------------------------------------------ */
/* Entry points used by main.cpp                                       */
/* ------------------------------------------------------------------ */
static const uint8_t *a7_rom = NULL;
static uint32_t a7_rom_size = 0;

extern "C" int a7800_init_game(const uint8_t *rom, uint32_t size)
{
    a7_rom = rom;
    a7_rom_size = size;

    /* Reset the pool allocator so a second entry gets a fresh block. */
    a7_pool_init();

    /* The 7800 only needs the low 16K as writable RAM (registers + zero page
     * + stack); everything from 0x4000 up is cartridge ROM read straight from
     * flash (XIP) via the slot map in Memory.cpp. So we allocate just 16K —
     * a full 64K would not fit in the shared 264K pool. */
    void *ram = a7_malloc(0x4000);
    if (!ram) { printf("[a7800] no pool for RAM\n"); return 0; }
    memset(ram, 0, 0x4000);
    a7800_set_memory((byte *)ram);

    a7_build_palette();

    if (!cartridge_Load((const byte *)rom, size)) {
        printf("[a7800] cartridge load failed\n");
        return 0;
    }
    prosystem_Reset();
    /* region_Reset() may have swapped in the PAL palette — rebuild the
     * RGB565 LUT so the palette matches the active region. */
    a7_build_palette();
    return 1;
}

extern "C" void a7800_run_frame(void)
{
    static byte input[17];
    /* One window for the whole frame — avoids 223 lcd_set_window calls. */
    display_stream_begin(0, 0, 320, 240);
    a7_build_input(input);
    prosystem_ExecuteFrame(input);
    display_stream_end();
}
