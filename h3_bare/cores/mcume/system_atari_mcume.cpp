/*
 * System layer for the MCUME/Virtual VCS x2600 core on pico-retro RP2040.
 * Replaces the MCUME emuapi glue (display/keys/malloc/file) with the
 * pico-retro display, onboard buttons and statically-allocated buffers.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "display.h"
#include "joypad.h"
}

/* 16-bit RGB565 pack helper (same formula as MCUME emuapi RGBVAL16). */
#define RGBVAL16(r, g, b) ( ((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | ((((b) >> 3) & 0x1F) << 0) )

/* Joystick bit masks (from MCUME emuapi.h, kept local). */
#define MASK_JOY2_RIGHT 0x0001
#define MASK_JOY2_LEFT  0x0002
#define MASK_JOY2_UP    0x0004
#define MASK_JOY2_DOWN  0x0008
#define MASK_JOY2_BTN   0x0010
#define MASK_KEY_USER1  0x0020
#define MASK_KEY_USER2  0x0040
#define MASK_KEY_USER3  0x0080

/* Core headers (C, wrapped) */
extern "C" {
#include "options.h"
#include "types.h"
#include "vmachine.h"
#include "display.h"
#include "vcs_display.h"
#include "collision.h"
#include "tiasound.h"
#include "resource.h"
#include "memory.h"
}

/* ------------------------------------------------------------------ */
/* MCUME emuapi stubs -> pico-retro                                    */
/* ------------------------------------------------------------------ */

/* Static memory pool for the handful of emu_Malloc calls the core makes.
 * VBuf is the 160x192 screen buffer; cart_small is only used to mirror
 * 2K ROMs (the core needs a 4K window). Larger ROMs are read straight
 * from flash (XIP) via theCart -> rom, saving ~32 KB of RAM.
 *
 * RAM SHARING: all Atari buffers live INSIDE the NES SCREEN buffer
 * (60 KB, system_pico_retro.cpp). NES and Atari never run at the same
 * time, so reusing the same memory saves ~56 KB of RAM. */
extern "C" uint8_t SCREEN[240][256];
static uint8_t *pool_buf     = &SCREEN[0][0];
static uint8_t *cart_small   = &SCREEN[0][0] + 160 * 192 + 8;
static uint8_t *scratch_buf  = &SCREEN[0][0] + 160 * 192 + 8 + 4096;
static uint8_t *cartram_buf  = &SCREEN[0][0] + 160 * 192 + 8 + 4096 + 4096;
static uint8_t *colvect_buf  = &SCREEN[0][0] + 160 * 192 + 8 + 4096 + 4096 + 1024;

/* Options that At2600.c used to define (file not ported). */
extern "C" {
int bThreadRunning = 0;
int pausing = 0;
int nOptions_SoundOn = 1;
int nOptions_Color = 1;
int nOptions_P1Diff = 1;
int nOptions_P2Diff = 1;
int nOptions_Interlace = 0;
int nOptions_Landscape = 0;
int nOptions_SkipFrames = 1;
}

extern "C" void *emu_Malloc(int size)
{
    if (size == 160 * 192 + 8) return pool_buf;      /* VBuf  */
    if (size == 4096)          return scratch_buf;   /* cartScratch */
    if (size == 1024)          return cartram_buf;   /* cartRam */
    if (size == 28 * 8)        return colvect_buf;   /* colvect */
    return pool_buf; /* fallback, unused */
}

extern "C" void emu_Free(void *pt) { (void)pt; }

/* ROM loading: data is already in flash, just copy it in. */
extern "C" unsigned int emu_LoadFile(const char *filepath, void *buf, int size)
{
    (void)filepath;
    (void)size;
    return 0;
}

extern "C" int emu_GetPad(void)
{
    /* joypad_buttons(): 0 = pressed, active LOW, NES bit order.
     * Returns MCUME masks read by keyjoy()/keycons()/keytrig():
     *   B (bit1)   -> MASK_KEY_USER1 = Game Reset
     *   Select (bit2) -> MASK_KEY_USER2 = Game Select
     *   A (bit0)   -> MASK_JOY2_BTN = fire
     *   d-pad      -> MASK_JOY2_* directions */
    uint8_t pad = joypad_buttons();
    int k = 0;
    if (!(pad & 0x10)) k |= MASK_JOY2_UP;       /* Up    */
    if (!(pad & 0x20)) k |= MASK_JOY2_DOWN;     /* Down  */
    if (!(pad & 0x80)) k |= MASK_JOY2_RIGHT;    /* Right */
    if (!(pad & 0x40)) k |= MASK_JOY2_LEFT;     /* Left  */
    if (!(pad & 0x01)) k |= MASK_JOY2_BTN;      /* A = fire */
    if (!(pad & 0x02)) k |= MASK_KEY_USER1;     /* B = Game Reset */
    if (!(pad & 0x04)) k |= MASK_KEY_USER2;     /* Select = Game Select */
    return k;
}

extern "C" int emu_ReadI2CKeyboard(void) { return 0; }
extern "C" void emu_printf(const char *text) { (void)text; }
extern "C" void emu_printi(int val) { (void)val; }

/* ------------------------------------------------------------------ */
/* Renderer: core writes pixels into VBuf (160x192) via the coltable   */
/* indirection. We translate the 24-bit colour_table into an RGB565    */
/* LUT once, then blit the whole screen to the display.                */
/* ------------------------------------------------------------------ */

static uint16_t atari_rgb565_lut[256];
extern "C" int tv_draw_count = 0;

/* The core's create_cmap() (in Display.c) walks the static colortable and
 * calls emu_SetPaletteEntry() for every index — that builds our RGB565 LUT. */
extern "C" void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index)
{
    atari_rgb565_lut[index] = RGBVAL16(r, g, b);
}

extern "C" void emu_DrawScreenPal16(unsigned char *VBuf, int width, int height, int stride)
{
    (void)stride;
    (void)width;
    (void)height;
    tv_draw_count++;
    /* Atari 2600 TIA pixels are not square: 160 points span the full 4:3
     * width, so each point is ~1.6x wider than tall. Stretch horizontally
     * to 256 (matching the NES window width), keep the native 192 rows so
     * characters/bottles/text have correct proportions. */
    const int SW = 160, SH = 192;
    const int W = 256, H = 192;
    int x0 = 32;                /* left edge of the 256px window */
    int y0 = (240 - H) / 2;     /* 24 - vertical centre */
    static uint8_t line[256];

    display_stream_begin(x0, y0, W, H);
    for (int sy = 0; sy < H; sy++) {
        const uint8_t *row = VBuf + sy * SW;
        for (int dx = 0; dx < W; dx++)
            /* VBuf holds raw TIA colour bytes (0..255): bits 7-4 = hue,
             * bits 3-0 = luminance, and the 256-entry colortable maps them
             * 1:1. Use the full (unmasked) stream so hues 4..15 survive. */
            line[dx] = row[(dx * SW) / W];
        display_stream_pixels_full(line, atari_rgb565_lut, W, 1);
    }
    display_stream_end();
}

extern "C" void emu_DrawVsync(void) {}

extern "C" int emu_FrameSkip(void) { return 0; }
extern "C" int emu_IsVga(void) { return 0; }

/* Sound: not wired to PWM yet (silent stub). */
extern "C" void emu_sndInit(void) {}
extern "C" void emu_sndPlaySound(int chan, int volume, int freq) { (void)chan; (void)volume; (void)freq; }
extern "C" void emu_sndPlayBuzz(int size, int val) { (void)size; (void)val; }

/* ------------------------------------------------------------------ */
/* Entry points used by main.cpp                                       */
/* ------------------------------------------------------------------ */

static int mcume_ready = 0;

extern "C" void atari2600_init(const uint8_t *rom, uint32_t size)
{
    /* Feed the ROM to the core. 2K carts must be mirrored into a 4K window;
     * everything else is read directly from flash (XIP) to save RAM. */
    extern int rom_size;
    extern BYTE *theCart;

    if (size == 2048) {
        memcpy(cart_small, rom, 2048);
        memcpy(cart_small + 2048, rom, 2048);
        theCart = cart_small;
        rom_size = 4096;
    } else {
        /* Read the ROM straight from flash (XIP) — the core banks by
         * re-pointing theRom into theCart, and byte reads work fine from
         * XIP memory (same as 32K carts). No RAM copy needed. */
        theCart = (BYTE *)rom;
        rom_size = (int)size;
        if (rom_size < 2048) {
            cart_small[0x0ffc] = 0x00;
            cart_small[0x0ffd] = 0xf0;
            rom_size = 4096;
        }
    }

    /* F4 32K: keep the full image (do not truncate to 16K). */
    if (rom_size == 8192)      base_opts.bank = 1;  /* F8  */
    else if (rom_size == 16384) base_opts.bank = 2; /* F6  */
    else if (rom_size == 32768) base_opts.bank = 6; /* F4  */
    else                        base_opts.bank = 0; /* 2K/4K */
    base_opts.tvtype = NTSC;
    base_opts.lcon = STICK;
    base_opts.rcon = STICK;

    init_machine();
    init_hardware();
    tv_on();

    mcume_ready = 1;
}

/* Run one emulated frame. mainloop() executes a fixed 7600 CPU iterations,
 * which is less than a full 2600 frame (~20000), so a single call may not
 * reach the VSYNC that triggers tv_display() -> blank screen. Loop mainloop()
 * until at least one frame has been drawn (capped so a game that never
 * generates VSYNC still yields and the menu/exit keeps working). */
extern "C" void atari2600_run_frame(void)
{
    if (!mcume_ready) return;
    extern void mainloop(void);
    extern void vcs_Input(int key);
    int before = tv_draw_count;
    int guard = 0;
    while (tv_draw_count == before && guard < 40) {
        vcs_Input(0);          /* reads joypad_buttons() into the core state */
        mainloop();
        guard++;
    }
}

extern "C" void atari2600_poll_joy(void)
{
    /* Input is read lazily inside keyjoy()/keycons()/keytrig() via
     * emu_GetPad(), so nothing to do here. */
}

/* Frame is already drawn to the display inside run_frame() (tv_display). */
extern "C" void atari2600_render(void) {}

/* Keep the same exit/helper names for drop-in compatibility. */
extern "C" void atari2600_set_difficulty(int p1_expert)
{
    /* MCUME: nOptions_P1Diff=1 -> P0 amateur (SWCHB bit6=0).
     * Old core: p1_expert=1 -> SWCHB bit6=1 (expert). So invert. */
    extern int nOptions_P1Diff;
    nOptions_P1Diff = p1_expert ? 0 : 1;
}
