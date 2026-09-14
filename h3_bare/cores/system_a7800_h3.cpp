// system_a7800_h3.cpp — host-слой ProSystem (Atari 7800) для H3 bare-metal.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
}

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#include "fb_text.h"
#include "a7800/ProSystem.h"
#include "a7800/Maria.h"
#include "a7800/Memory.h"
#include "a7800/Cartridge.h"
#include "a7800/Region.h"

static uint16_t a7_pal_rgb565[256];
static uint8_t a7_rom_data[1024 * 1024];
static uint32_t a7_rom_size = 0;

extern byte palette_data[768];

static void a7_build_palette(void) {
    for (int i = 0; i < 256; i++)
        a7_pal_rgb565[i] = (palette_data[i * 3] >> 3 << 11) |
                           (palette_data[i * 3 + 1] >> 2 << 5) |
                           (palette_data[i * 3 + 2] >> 3);
}

void maria_LineReady(const byte* line, int length) {
    if (length > 320) length = 320;
    int top = (int)maria_visibleArea.top;
    int h   = (int)maria_visibleArea.bottom - top + 1;
    int sy  = (int)maria_scanline - top;
    if (sy < 0 || h <= 0) return;
    int y0 = (sy * EMU_H) / h;
    int y1 = ((sy + 1) * EMU_H) / h;
    if (y0 >= EMU_H) return;
    if (y1 > EMU_H) y1 = EMU_H;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < length; x++)
            EMU_FB[y * EMU_W + x] = a7_pal_rgb565[line[x] & 0xFF];
}

extern "C" {
#include "fb_text.h"
}

static void a7_build_input(byte* input, uint8_t pad) {
    input[0] = (pad & 0x80) ? 0 : 1;
    input[1] = (pad & 0x40) ? 0 : 1;
    input[2] = (pad & 0x20) ? 0 : 1;
    input[3] = (pad & 0x10) ? 0 : 1;
    input[4] = (pad & 0x01) ? 0 : 1;
    input[5] = (pad & 0x02) ? 0 : 1;
    for (int i = 6; i < 12; i++) input[i] = 0;
    input[12] = 0;
    input[13] = (pad & 0x04) ? 0 : 1;
    input[14] = 0;
    input[15] = 0;
    input[16] = 0;
}

static uint8_t pad_from_kbd(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint8_t pad = 0xFF;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) pad &= ~0x10;
        if (sc == 81) pad &= ~0x20;
        if (sc == 80) pad &= ~0x40;
        if (sc == 79) pad &= ~0x80;
        if (sc == 29) pad &= ~0x01;
        if (sc == 27) pad &= ~0x02;
        if (sc == 22) pad &= ~0x04;
        if (sc == 40) pad &= ~0x08;
    }
    return pad;
}

extern "C" int a7800_init_game(const uint8_t* rom, uint32_t size) {
    if (size > sizeof(a7_rom_data)) size = sizeof(a7_rom_data);
    memcpy(a7_rom_data, rom, size);
    a7_rom_size = size;

    a7_build_palette();

    if (!cartridge_Load(a7_rom_data, size)) {
        printf("[a7800] cartridge load failed\n");
        return 0;
    }
    prosystem_Reset();
    a7_build_palette();
    printf("[a7800] loaded size=%d\n", (int)size);
    return 1;
}

extern "C" int a7800_run_frame(void) {
    static byte input[17];
    byte pad = pad_from_kbd();
    a7_build_input(input, pad);
    prosystem_ExecuteFrame(input);
    return 0;
}