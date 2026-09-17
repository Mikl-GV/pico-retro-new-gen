// ngp_host.cpp — Neo Geo Pocket / Pocket Color (RACE core) for H3 bare-metal
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "emu.h"
#include "ngp_host.h"
#include "h3_hs_timer.h"

extern "C" {
#include "usb_kbd.h"
}

// Graphics buffer
#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// RACE forward declarations
#include "main.h"
#include "memory.h"
#include "tlcs900h.h"
#include "input.h"
#include "graphics.h"
#include "flash.h"
#include "neopopsound.h"
#include "sound.h"

// BIOS ROM data (koyote.bin) — defines the koyote_bin symbol
#include "koyote_bin.h"

// Options stub for graphics.cpp Thor renderer
#define HICOLOR_OPTION 0
int options[8] = {0};

// RACE memory globals — memory.h declares them as extern;
// the definitions live here (see ngp_host.cpp design)
unsigned char __attribute__((aligned(4))) mainrom[4*1024*1024];
unsigned char __attribute__((aligned(4))) cpurom[256*1024];
unsigned char *cpuram = 0;
unsigned char __attribute__((aligned(4))) mainram[(64+32+128)*1024];
unsigned char realBIOSloaded = 0;

// drawBuffer — must be accessible as extern (graphics.cpp uses it)
// We define it with SIZEX pitch: each scanline uses SIZEX entries
unsigned short __attribute__((aligned(4))) drawBuffer[SIZEX * NGPC_SIZEY];

// Stub SDL surface
struct SDL_Surface { int w; int h; unsigned short* pixels; };
static SDL_Surface g_screen = { SIZEX, SIZEY, drawBuffer };
SDL_Surface* screen = &g_screen;
SDL_Surface* actualScreen = &g_screen;

// NGP state — m_bIsActive/m_emuInfo/m_sysInfo and ngpInputState are
// defined in main.cpp / input.cpp
extern int m_bIsActive;
extern EMUINFO m_emuInfo;
extern SYSTEMINFO m_sysInfo[NR_OF_SYSTEMS];
extern unsigned char ngpInputState;
BOOL mute = TRUE;

#define HOST_FPS 60

static int ngp_input_state(void) {
    uint8_t raw[6];
    int n = usb_kbd_get_raw(raw, 6);
    unsigned char state = 0;
    for (int i = 0; i < n; i++) {
        switch (raw[i]) {
            case 82: state |= 0x01; break; // Up
            case 81: state |= 0x02; break; // Down
            case 80: state |= 0x04; break; // Left
            case 79: state |= 0x08; break; // Right
            case 29: state |= 0x10; break; // A (Z, HID 0x1D)
            case 27: state |= 0x20; break; // B (X, HID 0x1B)
            case 22: state |= 0x40; break; // Select (S, HID 0x16)
            case 40: state |= 0x80; break; // Start (Enter, HID 0x28)
        }
    }
    return state;
}

void UpdateInputState(void) {
    ngpInputState = ngp_input_state();
}

// Stub SDL
extern "C" void SDL_LockSurface(void*) {}
extern "C" void SDL_UnlockSurface(void*) {}
extern "C" unsigned int SDL_GetTicks(void) {
    return h3_hs_timer_lo_us() / 1000;
}

// Sound stubs — only for functions not provided by neopopsound.cpp
int initSound() { return 0; }
void soundCleanup() {}
void soundStep(int) {}
void soundOutput() {}
int osd_start_audio_stream(int) { return 0; }
void osd_stop_audio_stream() {}
int osd_update_audio_stream(short*) { return 0; }
void osd_set_mastervolume(int) {}
int osd_get_mastervolume() { return 0; }
void ngpSoundStart() {}
void ngpSoundExecute() {}
void ngpSoundOff() {}
void ngpSoundInterrupt() {}
BOOL system_sound_init(void) { return TRUE; }
void system_VBL(void) {}
void dac_write(unsigned char) {}
void increaseVolume() {}
void decreaseVolume() {}
void writeSaveGameFile() {}

// BIOS font
extern unsigned char sysfont[8*256];
extern void ngpBiosSYSFONTSET(unsigned char *pt, char trans, char font);

static void blit_to_fb(void) {
    // Рисуем 160x152 в левый верхний угол EMU_FB (320x240) —
    // emu_scale(160,152) читает именно оттуда (паттерн как у Game Boy)
    for (int y = 0; y < NGPC_SIZEY; y++) {
        for (int x = 0; x < NGPC_SIZEX; x++) {
            EMU_FB[y * EMU_W + x] = drawBuffer[y * SIZEX + x];
        }
    }
}

extern "C" int ngp_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) return 0;
    if (size > 4*1024*1024) size = 4*1024*1024;

    memset(mainrom, 0, sizeof(mainrom));
    memcpy(mainrom, rom, size);

    memset(cpurom, 0, sizeof(cpurom));
    memset(mainram, 0, sizeof(mainram));

    // Init palette lookup table
    for (int r = 0; r < 32; r++)
        for (int g = 0; g < 32; g++)
            for (int b = 0; b < 32; b++)
                totalpalette[b*256 + g*16 + r] = (r << 10) | (g << 5) | b;

    m_bIsActive = FALSE;
    initSysInfo();
    m_emuInfo.romSize = size;
    strcpy(m_emuInfo.RomFileName, "rom");

    // Детекция машины должна идти ДО mainemuinit() — как в эталонном RACE:
    // initRom() вызывает SetEmu(m) перед mainemuinit(), чтобы mem_init() и
    // graphics_init() сразу знали NGP / NGPC.
    if (!initRom()) {
        printf("NGP: init failed (bad ROM?)\n");
        return 0;
    }
    setFlashSize(size);
    printf("NGP: %s size=%d\n",
           (m_emuInfo.machine == NGPC) ? "NGPC" : "NGP", (int)size);
    return 1;
}

extern "C" void ngp_run_frame(void) {
    if (!m_bIsActive) return;
    // Полный кадр NGPC = 198 сканлайнов x 515 тактов = 101970.
    // Важно: ровно один кадр, БЕЗ запаса — иначе дрейф ~50 линий/с
    // и чёрная полоса ползёт снизу вверх. Фаза стабильна (ngOverflow).
    // Один блит в кадр делает graphics_paint() при scanlineY==151 (VBlank),
    // когда все 152 строки уже нарисованы — здесь НЕ блинкуем повторно.
    tlcs_execute(515 * 198);
}

// Graphics override for graphics_paint — must be C-linkage
extern "C" void graphics_paint_impl(void);
void graphics_paint_impl(void) {
    // Called from graphics.cpp at VBlank — blits drawBuffer to EMU_FB
    blit_to_fb();
}