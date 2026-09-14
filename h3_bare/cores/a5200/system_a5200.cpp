// System layer for the Atari 5200 core (pico5200 from MCUME) on pico-retro.
// Bridges the 5200 emulator core to ILI9341 display, joypad, and shared RAM pool.
// The 5200 needs a 64K memory image; we allocate it from the shared NES buffers
// (SCREEN + ChrBuf) to save RAM — only one emulator runs at a time.
#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "display.h"
#include "joypad.h"
}

extern "C" {
#include "atari.h"
#include "atari5200.h"
#include "antic.h"
#include "gtia.h"
#include "cpu.h"
#include "emucfg.h"
#include "emuapi.h"
}

// ------------------------------------------------------------------
// Shared memory pool — reuses buffers from the NES layer.
// The core needs 64K; we carve it from SCREEN (60K) + ChrBuf (4K).
// ------------------------------------------------------------------
extern "C" uint8_t SCREEN[240][256];
extern "C" uint8_t ChrBuf[];
extern "C" uint8_t RAM[];
extern "C" uint8_t SRAM[];

static struct {
    uint8_t *base;
    int      size;
    int      used;
} a5_pool[] = {
    { (uint8_t *)&SCREEN[0][0], 240 * 256, 0 },
    { ChrBuf,                   256 * 2 * 8 * 8, 0 },
    { RAM,                      0x2000, 0 },
    { SRAM,                     0x2000, 0 },
};
#define A5_POOL_REGS (sizeof(a5_pool) / sizeof(a5_pool[0]))

// Reset pool allocator for a fresh game.
static void a5_pool_init(void)
{
    for (unsigned i = 0; i < A5_POOL_REGS; i++) a5_pool[i].used = 0;
}

// Allocate from the pool (linear bump allocator per segment).
static void *a5_malloc(int size)
{
    for (unsigned i = 0; i < A5_POOL_REGS; i++) {
        if (a5_pool[i].used + size <= a5_pool[i].size) {
            void *p = a5_pool[i].base + a5_pool[i].used;
            a5_pool[i].used += size;
            return p;
        }
    }
    printf("[a5200] pool exhausted for %d bytes\n", size);
    return NULL;
}

// ------------------------------------------------------------------
// Core globals required by the 5200 emulator.
// ------------------------------------------------------------------
extern "C" unsigned char *memory = 0;        // 64K image (allocated from pool)
extern "C" const unsigned char *at5_rom = 0; // embedded cartridge ROM
extern "C" int at5_rom_size = 0;
extern "C" int ik = 0;                       // joypad mask (set per frame)

// ------------------------------------------------------------------
// emuapi stubs — system callbacks the core expects.
// All use a5_ prefix to avoid clashes with the Atari 2600 layer.
// ------------------------------------------------------------------
extern "C" void *a5_Malloc(int size) { return a5_malloc(size); }
extern "C" void a5_Free(void *ptr) { (void)ptr; }
extern "C" void a5_printf(const char *text) { (void)text; }
extern "C" void a5_printi(int val) { (void)val; }
extern "C" int a5_ReadI2CKeyboard(void) { return 0; }

// State save/load stubs — not used on pico-retro.
extern "C" void StateSav_SaveUBYTE(const void *v) { (void)v; }
extern "C" void StateSav_SaveUWORD(const void *v) { (void)v; }
extern "C" void StateSav_SaveINT(const void *v) { (void)v; }
extern "C" void StateSav_ReadUBYTE(const void *v, int size) { (void)v; (void)size; }
extern "C" void StateSav_ReadUWORD(const void *v, int size) { (void)v; (void)size; }
extern "C" void StateSav_ReadINT(const void *v, int size) { (void)v; (void)size; }
extern "C" void MEMORY_StateSave(void) {}
extern "C" void MEMORY_StateRead(void) {}
extern "C" int emu_FileOpen(const char *f, const char *m) { (void)f; (void)m; return 0; }
extern "C" int emu_FileGetc(int h) { (void)h; return 0xFF; }
extern "C" int emu_FileSeek(int h, int s, int o) { (void)h; (void)s; (void)o; return 0; }
extern "C" int emu_FileTell(int h) { (void)h; return 0; }
extern "C" void emu_FileClose(int h) { (void)h; }
extern "C" unsigned int emu_FileSize(const char *f) { (void)f; return 0; }
extern "C" int emu_FileRead(void *b, int s, int h) { (void)b; (void)s; (void)h; return 0; }

// ------------------------------------------------------------------
// Renderer: 256-colour LUT + line-by-line blit at 320x240.
// ------------------------------------------------------------------
static uint16_t a5_pal_rgb565[PALETTE_SIZE];
static uint16_t a5_line_rgb[320];

// Register one palette entry (RGB → RGB565).
extern "C" void a5_PaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index)
{
    if (index < PALETTE_SIZE)
        a5_pal_rgb565[index] = RGB565(r >> 3, g >> 2, b >> 3);
}

// Render one scanline to the display via streaming.
extern "C" void a5_DrawLinePal16(unsigned char *VBuf, int width, int height, int line)
{
    (void)width; (void)height;
    if (line < 0 || line >= 240) return;
    for (int x = 0; x < 320; x++)
        a5_line_rgb[x] = a5_pal_rgb565[VBuf[x] & 0xFF];
    display_stream_begin(0, line, 320, 1);
    display_stream_pixels16(a5_line_rgb, 320, 1);
    display_stream_end();
}

// VSync callback — no overlay needed (5200 fills the full 320x240).
extern "C" void a5_DrawVsync(void) {}

// ------------------------------------------------------------------
// Input: map NES-style joypad to MASK_JOY2_* / MASK_KEY_USER*.
// ------------------------------------------------------------------
extern "C" int a5_GetPad(void)
{
    uint8_t pad = joypad_buttons();   // 0 = pressed, NES bit order
    int k = 0;
    if (!(pad & 0x10)) k |= MASK_JOY2_UP;
    if (!(pad & 0x20)) k |= MASK_JOY2_DOWN;
    if (!(pad & 0x40)) k |= MASK_JOY2_LEFT;
    if (!(pad & 0x80)) k |= MASK_JOY2_RIGHT;
    if (!(pad & 0x01)) k |= MASK_JOY2_BTN;      // A = fire
    if (!(pad & 0x02)) k |= MASK_KEY_USER1;     // B = Pause (side)
    if (!(pad & 0x04)) k |= MASK_KEY_USER2;     // Select = Start
    if (!(pad & 0x08)) k |= MASK_KEY_USER3;     // Start = key 1
    return k;
}

// ------------------------------------------------------------------
// Entry points called by main.cpp.
// ------------------------------------------------------------------

// Initialise the 5200 emulator with the embedded cartridge ROM.
extern "C" int a5200_init_game(const uint8_t *rom, uint32_t size)
{
    a5_pool_init();
    for (int i = 0; i < PALETTE_SIZE; i++) a5_pal_rgb565[i] = 0;

    memory = (unsigned char *)a5_malloc(MEMORY_SIZE);
    if (!memory) { printf("[a5200] no pool for 64K memory\n"); return 0; }

    at5_rom = rom;
    at5_rom_size = (int)size;
    ik = 0;

    at5_Init();
    at5_Start((char *)"cart");
    return 1;
}

// Run one frame of Atari 5200 emulation.
extern "C" void a5200_run_frame(void)
{
    at5_Input(0);
    at5_Step();
}