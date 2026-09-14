// system_atari_h3.cpp — host-слой MCUME (Virtual VCS) для H3 bare-metal.
// Заменяет pico-retro display/joypad на HDMI-фреймбуфер и USB-клавиатуру.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
}

// framebuffer для эмулятора: 160x192 RGB565
#define EMU_FB ((uint16_t*)0x5F800000)
#define EMU_W 160
#define EMU_H 192

// MCUME core headers
extern "C" {
#include "mcume/options.h"
#include "mcume/types.h"
#include "mcume/vmachine.h"
#include "mcume/vcs_display.h"
#include "mcume/collision.h"
#include "mcume/tiasound.h"
#include "mcume/resource.h"
#include "mcume/memory.h"
}

#define RGBVAL16(r, g, b) (((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | ((((b) >> 3) & 0x1F) << 0))

// Статический пул памяти для emu_Malloc
static uint8_t pool[160 * 192 + 8 + 4096 + 4096 + 1024 + 28 * 8];
static uint8_t* pool_ptr = pool;

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

extern "C" void* emu_Malloc(int size) {
    void* r = (void*)pool_ptr;
    pool_ptr += size;
    return r;
}

extern "C" void emu_Free(void*) {}

extern "C" unsigned int emu_LoadFile(const char*, void*, int) { return 0; }

extern "C" int emu_GetPad(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) k |= 0x0004;  // Up
        if (sc == 81) k |= 0x0008;  // Down
        if (sc == 80) k |= 0x0002;  // Left
        if (sc == 79) k |= 0x0001;  // Right
        if (sc == 29) k |= 0x0010;  // Z = fire
        if (sc == 22) k |= 0x0040;  // S = Select
        if (sc == 40) k |= 0x0020;  // Enter = Reset
    }
    return k;
}

extern "C" int emu_ReadI2CKeyboard(void) { return 0; }
extern "C" void emu_printf(const char* text) { printf("%s", text); }
extern "C" void emu_printi(int val) { printf("%d", val); }

static uint16_t atari_rgb565_lut[256];
extern "C" int tv_draw_count = 0;

extern "C" void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index) {
    atari_rgb565_lut[index] = RGBVAL16(r, g, b);
}

extern "C" void emu_DrawScreenPal16(unsigned char* VBuf, int width, int height, int stride) {
    (void)stride;
    tv_draw_count++;
    uint16_t* fb = EMU_FB;
    for (int y = 0; y < height && y < EMU_H; y++) {
        for (int x = 0; x < width && x < EMU_W; x++) {
            fb[y * EMU_W + x] = atari_rgb565_lut[VBuf[y * width + x]];
        }
    }
}

extern "C" void emu_DrawVsync(void) {}
extern "C" int emu_FrameSkip(void) { return 0; }
extern "C" int emu_IsVga(void) { return 0; }
extern "C" void emu_sndInit(void) {}
extern "C" void emu_sndPlaySound(int, int, int) {}
extern "C" void emu_sndPlayBuzz(int, int) {}

static int mcume_ready = 0;

extern "C" void atari2600_init(const uint8_t* rom, uint32_t size) {
    extern int rom_size;
    extern BYTE* theCart;
    pool_ptr = pool;  // reset allocator

    // 2K carts mirrored to 4K window
    if (size == 2048) {
        uint8_t* cart_small = (uint8_t*)emu_Malloc(4096);
        memcpy(cart_small, rom, 2048);
        memcpy(cart_small + 2048, rom, 2048);
        theCart = cart_small;
        rom_size = 4096;
    } else {
        theCart = (BYTE*)rom;
        rom_size = (int)size;
    }

    if (rom_size == 8192)      base_opts.bank = 1;
    else if (rom_size == 16384) base_opts.bank = 2;
    else if (rom_size == 32768) base_opts.bank = 6;
    else                        base_opts.bank = 0;
    base_opts.tvtype = NTSC;
    base_opts.lcon = STICK;
    base_opts.rcon = STICK;

    init_machine();
    init_hardware();
    tv_on();
    mcume_ready = 1;
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
    printf("MCUME: size=%d bank=%d\n", rom_size, base_opts.bank);
}

extern "C" void atari2600_run_frame(void) {
    if (!mcume_ready) return;
    extern void mainloop(void);
    int before = tv_draw_count;
    int guard = 0;
    while (tv_draw_count == before && guard < 40) {
        mainloop();
        guard++;
    }
}

extern "C" void atari2600_render(void) {}
extern "C" void atari2600_poll_joy(void) {}

extern "C" void atari2600_set_difficulty(int p1_expert) {
    extern int nOptions_P1Diff;
    nOptions_P1Diff = p1_expert ? 0 : 1;
}