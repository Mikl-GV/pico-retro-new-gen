// snes_host.cpp — host-слой Snes9x 2005 для bare-metal H3.
//
// ROM подаётся буфером (rom_browser загружает в 0x50000000).
// Инициализация: S9xInitMemory → S9xInitAPU → S9xInitDisplay → S9xInitGFX
//   → S9xInitSound → LoadROM (прямое копирование в Memory.ROM)
// Один кадр: S9xMainLoop() (без LAGFIX — возвращается после VBlank).
// Рендер: GFX.Screen (RGB565, pitch = GFX.Pitch байт) → EMU_FB.

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "cpuexec.h"
#include "apu.h"
#include "display.h"
#include "cheats.h"
#include "ppu.h"
#include "spc7110.h"
#include "srtc.h"
#include "usb_kbd.h"
#include "emu.h"
#include "fb_text.h"
#include "h3_hs_timer.h"
}

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- состояние host-слоя ----
static int g_loaded = 0;

// Joypad-данные (читаются S9xReadJoypad, заполняются build_input)
static uint32_t g_joydata = 0;

// Глобалы, которые в оригинале определяет libretro.c
extern "C" {
bool overclock_cycles = false;
int one_c = 6;
int slow_one_c = 8;
int two_c = 12;
bool reduce_sprite_flicker = false;
}

// ---- S9xInitDisplay/S9xDeinitDisplay (определены в libretro.c; переносим сюда) ----
extern "C" void* malloc(size_t sz);
extern "C" void free(void* p);

extern "C" void S9xInitDisplay(void) {
    int32_t h = IMAGE_HEIGHT;
    int32_t safety = 32;
    GFX.Pitch = IMAGE_WIDTH * 2;
    GFX.Screen_buffer = (uint8_t*)malloc(GFX.Pitch * h + safety);
    GFX.SubScreen_buffer = (uint8_t*)malloc(GFX.Pitch * h + safety);
    GFX.ZBuffer_buffer = (uint8_t*)malloc((GFX.Pitch >> 1) * h + safety);
    GFX.SubZBuffer_buffer = (uint8_t*)malloc((GFX.Pitch >> 1) * h + safety);
    GFX.Screen = GFX.Screen_buffer + safety;
    GFX.SubScreen = GFX.SubScreen_buffer + safety;
    GFX.ZBuffer = GFX.ZBuffer_buffer + safety;
    GFX.SubZBuffer = GFX.SubZBuffer_buffer + safety;
    GFX.Delta = (GFX.SubScreen - GFX.Screen) >> 1;
}

extern "C" void S9xDeinitDisplay(void) {
    free(GFX.Screen_buffer);
    free(GFX.SubScreen_buffer);
    free(GFX.ZBuffer_buffer);
    free(GFX.SubZBuffer_buffer);
    GFX.Screen = NULL;
    GFX.Screen_buffer = NULL;
    GFX.SubScreen = NULL;
    GFX.SubScreen_buffer = NULL;
    GFX.ZBuffer = NULL;
    GFX.ZBuffer_buffer = NULL;
    GFX.SubZBuffer = NULL;
    GFX.SubZBuffer_buffer = NULL;
}

// ---- колбэки для ppu.c (требует display.h) ----
extern "C" uint32_t S9xReadJoypad(int32_t port) {
    if (port == 0) return g_joydata;
    return 0;
}

extern "C" bool S9xReadMousePosition(int32_t w, int32_t* x, int32_t* y, uint32_t* b) {
    (void)w; (void)x; (void)y; (void)b;
    return false;
}

extern "C" bool S9xReadSuperScopePosition(int32_t* x, int32_t* y, uint32_t* b) {
    (void)x; (void)y; (void)b;
    return true;
}

extern "C" bool JustifierOffscreen(void) {
    return false;
}

extern "C" void JustifierButtons(uint32_t* j) {
    (void)j;
}

// ---- ввод: USB-клавиатура → SNES геймпад ----
static void build_input(void) {
    g_joydata = 0;
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        // Крестовина
        if (sc == 82) g_joydata |= SNES_UP_MASK;
        if (sc == 81) g_joydata |= SNES_DOWN_MASK;
        if (sc == 80) g_joydata |= SNES_LEFT_MASK;
        if (sc == 79) g_joydata |= SNES_RIGHT_MASK;
        // Кнопки
        if (sc == 29) g_joydata |= SNES_B_MASK;      // Z = B
        if (sc == 27) g_joydata |= SNES_Y_MASK;      // X = Y
        if (sc == 4)  g_joydata |= SNES_A_MASK;      // A = A
        if (sc == 22) g_joydata |= SNES_X_MASK;      // S = X
        if (sc == 20) g_joydata |= SNES_TL_MASK;     // Q = L
        if (sc == 26) g_joydata |= SNES_TR_MASK;     // W = R
        if (sc == 44) g_joydata |= SNES_SELECT_MASK; // Space = Select
        if (sc == 40) g_joydata |= SNES_START_MASK;  // Enter = Start
    }
}

// ---- инициализация ----
extern "C" int snes_init_game(const uint8_t* rom, uint32_t size) {
    printf("Snes9x: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    gb_heap_reset();

    // Настройки по умолчанию (как в init_sfc_setting libretro.c)
    memset(&Settings, 0, sizeof(Settings));
    Settings.JoystickEnabled = false;
    Settings.SoundPlaybackRate = 32040;
    Settings.CyclesPercentage = 100;
    Settings.DisableSoundEcho = false;
    Settings.InterpolatedSound = true;
    Settings.APUEnabled = true;
    Settings.H_Max = 341;     // SNES_CYCLES_PER_SCANLINE
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.DisableMasterVolume = false;
    Settings.Mouse = true;
    Settings.SuperScope = true;
    Settings.MultiPlayer5 = true;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.ApplyCheats = true;
    Settings.HBlankStart = (256 * Settings.H_Max) / 341;
    Settings.Mute = true;     // без звука

    // Инициализация памяти
    if (!S9xInitMemory()) {
        printf("Snes9x: S9xInitMemory failed\n");
        return 0;
    }
    S9xInitAPU();
    S9xInitDisplay();
    if (!S9xInitGFX()) {
        printf("Snes9x: S9xInitGFX failed\n");
        S9xDeinitMemory();
        return 0;
    }
    S9xInitSound();

    IPPU.RenderThisFrame = true;

    // Загрузка ROM напрямую в Memory.ROM
    // LoadROM (с LOAD_FROM_MEMORY) делает: header-skip → memcpy → InitROM → Reset
    // Создаём минимальный retro_game_info
    struct retro_game_info game;
    game.data = rom;
    game.size = size;
    game.path = NULL;
    game.meta = NULL;

    if (!LoadROM(&game)) {
        printf("Snes9x: LoadROM failed\n");
        S9xDeinitGFX();
        S9xDeinitDisplay();
        S9xDeinitAPU();
        S9xDeinitMemory();
        return 0;
    }

    // Не вызываем S9xSetPlaybackRate — аудио отключено (Settings.Mute=true)
    g_loaded = 1;
    printf("Snes9x: ROM loaded, LoROM=%d HiROM=%d\n",
           (int)Memory.LoROM, (int)Memory.HiROM);
    return 1;
}

// ---- один кадр ----
extern "C" void snes_run_frame(void) {
    if (!g_loaded) return;

    build_input();

    // Устанавливаем геймпад для S9xUpdateJoypads (читает S9xReadJoypad)
    IPPU.Joypads[0] = g_joydata;

    // Запускаем один фрейм ядра:
    // IPPU.RenderThisFrame = true → S9xMainLoop крутит CPU до VBlank, затем
    // сама ставит false и выходит. Ставить надо КАЖДЫЙ кадр, иначе второй
    // вызов S9xMainLoop вернётся сразу без эмуляции.
    IPPU.RenderThisFrame = true;

    // Safety: если ROM зависнет, после ~60M инструкций (30 млн. циклов) —
    // форсируем выход. В норме кадр занимает < 4M инструкций.
    S9xMainLoop();

    // Рендер: GFX.Screen → EMU_FB
    // Формат: RGB565 (BUILD_PIXEL). GFX.Pitch — ширина буфера в байтах,
    // IPPU.RenderedScreenWidth/Height — активная область.
    int w = IPPU.RenderedScreenWidth;
    int h = IPPU.RenderedScreenHeight;
    if (w <= 0 || w > 512) w = 256;
    if (h <= 0 || h > 240) h = 224;

    int pitch = GFX.Pitch;
    const uint8_t* src = GFX.Screen;

    for (int y = 0; y < h && y < EMU_H; y++) {
        const uint16_t* s = (const uint16_t*)(src + y * pitch);
        uint16_t* d = EMU_FB + y * EMU_W;
        int xw = (w > EMU_W) ? EMU_W : w;
        for (int x = 0; x < xw; x++)
            d[x] = s[x];
        for (int x = xw; x < EMU_W; x++)
            d[x] = 0;
    }
    for (int y = h; y < EMU_H; y++)
        for (int x = 0; x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = 0;
}

// ---- стоп ----
extern "C" void snes_stop(void) {
    if (!g_loaded) return;
    Del7110Gfx();
    S9xDeinitGFX();
    S9xDeinitDisplay();
    S9xDeinitAPU();
    S9xDeinitMemory();
    g_loaded = 0;
    printf("Snes9x: stopped\n");
}

// ---- точка входа для emu.c ----
extern "C" void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    printf("Snes9x: starting...\n");
    fb_clear(); fb_flush();
    if (snes_init_game(rom, size) != 1) {
        printf("SNES: init failed\n");
        return;
    }
    emu_set_border_color(0x000B0C18);   // тёмно-синеватый (SNES)
    uint8_t raw_keys[6];
    uint32_t esc_hold_us = 0;
    emu_throttle_reset();
    for (;;) {
        snes_run_frame();
        emu_throttle();
        emu_scale(IPPU.RenderedScreenWidth > 0 ? IPPU.RenderedScreenWidth : 256,
                  IPPU.RenderedScreenHeight > 0 ? IPPU.RenderedScreenHeight : 224);
        fb_flush();
        // ESC — удержание ~0.9 с на выход
        int nk = usb_kbd_get_raw(raw_keys, 6);
        int esc = 0;
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) { esc = 1; break; }
        if (esc) {
            if (!esc_hold_us) esc_hold_us = h3_hs_timer_lo_us();
            else if (h3_hs_timer_lo_us() - esc_hold_us > 900000) goto exit;
        } else {
            esc_hold_us = 0;
        }
    }
exit:
    snes_stop();
    fb_clear(); fb_flush();
}