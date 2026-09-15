// gameboy_host.cpp — host-слой Game Boy / Game Boy Color (binjgb) для H3.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
}

#define EMU_FB ((uint16_t*)0x5F800000)
#define GB_W   160
#define GB_H   144
#define EMU_W  320
#define EMU_H  240

#include "gameboy/emulator.h"
#include "gameboy/common.h"

extern "C" {
void gb_heap_reset(void);
int printf(const char* fmt, ...);
}

static Emulator* g_emu = NULL;

extern "C" int gb_init_game(const uint8_t* rom, uint32_t size) {
    gb_heap_reset();

    EmulatorInit init;
    memset(&init, 0, sizeof(init));
    init.rom.data = (uint8_t*)rom;  // ROM напрямую, без копирования
    init.rom.size = size;
    init.audio_frequency = 0;
    init.random_seed = 42;
    init.force_dmg = FALSE;
    init.cgb_color_curve = CGB_COLOR_CURVE_NONE;

    g_emu = emulator_new(&init);
    if (!g_emu) { printf("[GB] emulator_new failed\n"); return 0; }

    printf("[GB] init ok, size=%u\n", (unsigned)size);
    return 1;
}

extern "C" void gb_run_frame(void) {
    if (!g_emu) return;
    EmulatorEvent events;
    do {
        events = emulator_run_until(g_emu, emulator_get_ticks(g_emu) + PPU_FRAME_TICKS);
    } while (!(events & EMULATOR_EVENT_NEW_FRAME));
}

extern "C" void gb_render_frame(void) {
    if (!g_emu) return;
    FrameBuffer* fb = emulator_get_frame_buffer(g_emu);
    if (!fb) return;

    // RGBA = (a<<24)|(b<<16)|(g<<8)|r
    for (int y = 0; y < GB_H && y < EMU_H; y++)
        for (int x = 0; x < GB_W && x < EMU_W; x++) {
            RGBA rgba = (*fb)[y * GB_W + x];
            uint16_t r5 = (rgba & 0xFF) >> 3;
            uint16_t g6 = ((rgba >> 8) & 0xFF) >> 2;
            uint16_t b5 = ((rgba >> 16) & 0xFF) >> 3;
            EMU_FB[y * EMU_W + x] = (r5 << 11) | (g6 << 5) | b5;
        }
}