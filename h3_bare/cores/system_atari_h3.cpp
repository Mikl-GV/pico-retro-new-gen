// system_atari_h3.cpp — host-слой MCUME (Virtual VCS) для H3.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "cheatdb.h"
}

#define EMU_FB ((uint16_t*)0x5F800000)
#define EMU_W 320
#define EMU_H 240

extern "C" {
#include "mcume/options.h"
#include "mcume/types.h"
#include "mcume/vmachine.h"
#include "mcume/vcs_display.h"
#include "mcume/collision.h"
#include "mcume/tiasound.h"
#include "mcume/resource.h"
#include "mcume/Memory.h"
}

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

    // Sega-геймпад: крестовина, A/B=Fire (дубль: у A2600 одна кнопка),
    // Start=Reset (запуск игры), Mode=Select (выбор игры/уровня).
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) k |= 0x0004;   // Up
    if (sp & 0x0002) k |= 0x0008;   // Down
    if (sp & 0x0004) k |= 0x0002;   // Left
    if (sp & 0x0008) k |= 0x0001;   // Right
    if (sp & 0x0010) k |= 0x0010;   // A -> Fire
    if (sp & 0x0020) k |= 0x0010;   // B -> Fire (дубль: у A2600 одна кнопка)
    if (sp & 0x0080) k |= 0x0020;   // Start -> Reset (старт игры, r155)
    if (sp & 0x0800) k |= 0x0040;   // Mode -> Select

    // Клавиатура -> A2600 (ремап через Settings → Keyboard remap)
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_UP, keys, n))    k |= 0x0004;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_DOWN, keys, n))  k |= 0x0008;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_LEFT, keys, n))  k |= 0x0002;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_RIGHT, keys, n)) k |= 0x0001;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_FIRE, keys, n))  k |= 0x0010;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_SELECT, keys, n)) k |= 0x0040;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_RESET, keys, n)) k |= 0x0020;
    return k;
}

extern "C" int emu_ReadI2CKeyboard(void) { return 0; }
extern "C" void emu_printf(const char* text) { printf("%s", text); }
extern "C" void emu_printi(int val) { printf("%d", val); }

static uint16_t atari_rgb565_lut[256];
int tv_draw_count = 0;

extern "C" void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index) {
    atari_rgb565_lut[index] = ((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | ((((b) >> 3) & 0x1F) << 0);
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
    extern void vcs_Input(int key);
    int before = tv_draw_count;
    int guard = 0;
    // RAW-читы: пишем байт каждый кадр (RIOT RAM 128 байт, адрес &0x7f)
    extern BYTE theRam[];
    int rc = cheats_raw_count();
    for (int i = 0; i < rc; i++) {
        uint32_t a; uint8_t v, c; int hc;
        if (cheats_raw_get(i, &a, &v, &c, &hc)) {
            a &= 0x7F;
            if (!hc || theRam[a] == c) theRam[a] = v;
        }
    }
    while (tv_draw_count == before && guard < 40) {
        vcs_Input(0);   // обновляет k = emu_GetPad() — иначе кнопки «застывают»
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