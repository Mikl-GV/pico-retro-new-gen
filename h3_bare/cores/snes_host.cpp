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
#include "soundux.h"
#include "display.h"
#include "cheats.h"
#include "ppu.h"
#include "spc7110.h"
#include "srtc.h"
#include "usb_kbd.h"
#include "remap.h"
#include "emu.h"
#include "emulog.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "cheatdb.h"
#include "i2s.h"
#include "fb_text.h"
#include "h3_hs_timer.h"
}

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);
extern "C" uint32_t g_s9x_guard;   // r175: safety SNES (cpuexec.c)

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- состояние host-слоя ----
static int g_loaded = 0;

// r1498: DE2 корректно читает 32-битные кадры (PXT), а 16-битные (RGB565)
// — шахматит (проверено на 32X/GBA/Lynx: битый DE2-16бит, CPU-копия чистая).
// SNES рендерит RGB565 в EMU_FB (256/512 колонок, страйд 320; r1658: hi-res
// 512 усредняется парами до 256 логических пикселей — раньше правые колонки
// просто не попадали в кадр); в snes_render_frame кадр
// конвертируется в XRGB8888-буфер и отдаётся DE2 GSU, который растягивает
// на всю высоту FB_H=600 с сохранением пропорций (dst_w = w*600/h, кламп
// 1024, dx по центру) — та же геометрия, что у программного emu_scale.
// Буфер упакован w×h (строки по w слов) — DE2 PITCH = INSIZE (урок r1495).
static uint32_t snes_rgb32[EMU_W * EMU_H] __attribute__((aligned(4), section(".noinit_fbn")));
// r1498: активен ли аппаратный DE2 GSU (для emu_run_snes — пропустить emu_scale).
// file-static — переживает сессии, сбрасывается в snes_init_game (урок r1496).
static int g_hw_scaler_active = 0;
// r1658 (A-07): ширина кадра в EMU_FB после приведения к логической:
// hi-res 512 усредняется парами до 256; остальные — как есть.
static int g_eff_w = 0;

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

    // Sega-геймпад (PCF8574): крестовина + A/B/C/X/Y/Z/Start/Mode
    // Таблица: A->A B->B C->L X->X Y->Y Z->R Start Mode->Select
    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) g_joydata |= SNES_UP_MASK;
    if (sp & 0x0002) g_joydata |= SNES_DOWN_MASK;
    if (sp & 0x0004) g_joydata |= SNES_LEFT_MASK;
    if (sp & 0x0008) g_joydata |= SNES_RIGHT_MASK;
    if (sp & 0x0010) g_joydata |= SNES_A_MASK;        // Sega A -> SNES A
    if (sp & 0x0020) g_joydata |= SNES_B_MASK;        // Sega B -> SNES B
    if (sp & 0x0040) g_joydata |= SNES_TL_MASK;       // Sega C -> L
    if (sp & 0x0080) g_joydata |= SNES_START_MASK;    // Start
    if (sp & 0x0100) g_joydata |= SNES_X_MASK;        // Sega X -> SNES X
    if (sp & 0x0200) g_joydata |= SNES_Y_MASK;        // Sega Y -> SNES Y
    if (sp & 0x0400) g_joydata |= SNES_TR_MASK;       // Sega Z -> R
    if (sp & 0x0800) g_joydata |= SNES_SELECT_MASK;   // Mode -> Select

    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    // Клавиатура — через переназначаемый ремап (Settings → Keyboard remap).
    // Дедолт: стрелки=D-Pad, Z=B X=Y A=A S=X Q=L W=R Space=Select Enter=Start.
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_UP, keys, n))    g_joydata |= SNES_UP_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_DOWN, keys, n))  g_joydata |= SNES_DOWN_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_LEFT, keys, n))  g_joydata |= SNES_LEFT_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_RIGHT, keys, n)) g_joydata |= SNES_RIGHT_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_B, keys, n))     g_joydata |= SNES_B_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_Y, keys, n))     g_joydata |= SNES_Y_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_A, keys, n))     g_joydata |= SNES_A_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_X, keys, n))     g_joydata |= SNES_X_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_L, keys, n))     g_joydata |= SNES_TL_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_R, keys, n))     g_joydata |= SNES_TR_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_SELECT, keys, n)) g_joydata |= SNES_SELECT_MASK;
    if (remap_kbd_pressed(REMAP_PLAT_SNES, BTN_START, keys, n)) g_joydata |= SNES_START_MASK;
}

// ---- инициализация ----
extern "C" int snes_init_game(const uint8_t* rom, uint32_t size) {
    printf("SNES: init size=%u\n", (unsigned)size);
    // r0.417 (M5 аудита): битый элемент (NULL/0) и ROM больше внутреннего
    // буфера ядра 8 МБ (MAX_ROM_SIZE) — не запускаем, как остальные host-слои.
    if (!rom || size == 0) { printf("SNES: no ROM\n"); return 0; }
    if (size > 0x800000) { printf("SNES: ROM too big (%u > 8MB)\n", (unsigned)size); return 0; }
    g_loaded = 0;
    gb_heap_reset();
    // r1498: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_hw_scaler_active = 0;
    g_eff_w = 0;

    // Настройки по умолчанию (как в init_sfc_setting libretro.c)
    memset(&Settings, 0, sizeof(Settings));
    Settings.JoystickEnabled = false;
    Settings.SoundPlaybackRate = 48000;
    i2s_dc_shift_set(6);   // SNES — непрерывный поток → ~120 Гц (как Lynx)
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
    Settings.Mute = false;   // r600: звук SNES на I2S (было true = «отключён»; см. S9xSetSoundMute)

    // Инициализация памяти
    if (!S9xInitMemory()) {
        printf("SNES: S9xInitMemory failed\n");
        return 0;
    }
    S9xInitAPU();
    S9xInitDisplay();
    if (!S9xInitGFX()) {
        printf("SNES: S9xInitGFX failed\n");
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
        printf("SNES: LoadROM failed\n");
        S9xDeinitGFX();
        S9xDeinitDisplay();
        S9xDeinitAPU();
        S9xDeinitMemory();
        return 0;
    }

    // Устанавливаем частоту звука (48000, I2S → MAX98357A)
    S9xSetPlaybackRate(48000);

    // Применяем отмеченные читы (SNES Game Genie / Pro Action Replay).
    // S9xAddCheat + Settings.ApplyCheats=true → ядро само патчит память каждый кадр.
    S9xDeleteCheats();
    {
        int ccnt = cheats_count();
        for (int i = 0; i < ccnt; i++) {
            if (!cheats_enabled(i)) continue;
            const char* code = cheats_code(i);
            if (!code || !code[0]) continue;
            uint32_t a; uint8_t v;
            if (S9xGameGenieToRaw(code, &a, &v)) {
                S9xAddCheat(true, false, a, v);
            } else if (S9xProActionReplayToRaw(code, &a, &v)) {
                S9xAddCheat(true, false, a, v);
            } else {
                printf("SNEScheat: bad code '%s'\n", code);
            }
        }
        if (ccnt) S9xApplyCheats();
    }

    g_loaded = 1;
    printf("SNES: ROM loaded, LoROM=%d HiROM=%d\n",
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

    // r175: safety-счётчик кадра (cpuexec.c: g_s9x_guard) — форсит finishedFrame
    g_s9x_guard = 0;
    S9xMainLoop();

    // Звук SNES на I2S (r600/r622). S9xMixSamples(sndbuf, sample_count) заполняет
    // СТЕРЕО: MixStereo пишет buffer[0..sample_count-1] как L,R чередуясь, т.е.
    // sample_count — число ОТСЧЁТОВ стерео (пар×2), и потребляет ровно столько.
    // r622: исправлена размерность (был cnt=800 «пар», но ядро заполняло 800
    // отсчётов=400 пар; читалось 800 пар → мусор/песок). Число пар считаем по
    // эталону (libretro.c): samples_per_frame = SoundPlaybackRate/refresh_rate с
    // дробным аккумулятором — это убирает дрейф и корректно ведёт PAL (50 Гц).
    {
        static int16_t sndbuf[4096];
        static float   acc = 0.0f;
        float refresh = (Settings.PAL ? 50.0f : 60.0f);
        float spf     = (float)Settings.SoundPlaybackRate / refresh;   // пар/кадр
        int   frames  = (int)spf;
        acc          += spf - (float)frames;
        if (acc > 1.0f) { frames++; acc -= 1.0f; }

        if (frames > 2048) frames = 2048;
        S9xMixSamples(sndbuf, frames << 1);   // отсчёты стерео
        // r1346 (Ш2.2): одна пакетная подача вместо цикла push_sample.
        // push_sample делает dsb на КАЖДУЮ пару (~800/кадр на NTSC), push_block —
        // один барьер на блок. Формат идентичен: интерлив L,R парами.
        i2s_push_block(sndbuf, (uint32_t)frames);
    }

    // Рендер: GFX.Screen → EMU_FB
    // Формат: RGB565 (BUILD_PIXEL). GFX.Pitch — ширина буфера в байтах,
    // IPPU.RenderedScreenWidth/Height — активная область.
    int w = IPPU.RenderedScreenWidth;
    int h = IPPU.RenderedScreenHeight;
    if (w <= 0 || w > 512) w = 256;
    if (h <= 0 || h > 240) h = 224;

    int pitch = GFX.Pitch;
    const uint8_t* src = GFX.Screen;

    // r1658 (A-07): hi-res 512 — усредняем пары пикселей в 256 логических
    // (RGB565-среднее по каналам). Раньше копировались только левые 320
    // колонок, правые 192 не попадали в кадр.
    int eff_w = (w > EMU_W) ? (w >> 1) : w;
    if (eff_w <= 0) eff_w = EMU_W;
    g_eff_w = eff_w;
    for (int y = 0; y < h && y < EMU_H; y++) {
        const uint16_t* s = (const uint16_t*)(src + y * pitch);
        uint16_t* d = EMU_FB + y * EMU_W;
        if (w > EMU_W) {
            for (int x = 0; x < eff_w; x++) {
                uint16_t a = s[2 * x];
                uint16_t b = (2 * x + 1 < w) ? s[2 * x + 1] : a;
                d[x] = (uint16_t)(((a & 0xF7DEu) >> 1) + ((b & 0xF7DEu) >> 1));
            }
        } else {
            for (int x = 0; x < eff_w; x++)
                d[x] = s[x];
        }
        for (int x = eff_w; x < EMU_W; x++)
            d[x] = 0;
    }
    for (int y = h; y < EMU_H; y++)
        for (int x = 0; x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = 0;
}

// ---- r1498: аппаратный DE2 GSU (вместо CPU emu_scale) ----
// Активен ли DE2 GSU (для emu_run_snes — пропустить emu_scale).
extern "C" int snes_hw_scaler_active(void) { return g_hw_scaler_active; }

// Рендер кадра: DE2 GSU растягивает 32-битную копию кадра на весь экран.
// Геометрия — как emu_scale (dst_h = FB_H = 600, dst_w = w*600/h, кламп
// 1024, dx по центру), «не ломать что работает». Слой настраивается один
// раз при первом кадре, дальше DE2 читает snes_rgb32 по DMA; CPU только
// конвертирует RGB565→XRGB8888 (дёшево, без интерполяции).
// Fallback — программный emu_scale из самого EMU_FB (g_hw_scaler_active=0).
extern "C" void snes_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void h3_de2_set_bkcolor(uint32_t argb);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    int w = g_eff_w > 0 ? g_eff_w : EMU_W;   // r1658: логическая ширина кадра
    int h = IPPU.RenderedScreenHeight;
    if (w > EMU_W) w = EMU_W;
    if (h <= 0 || h > EMU_H) h = EMU_H;
    if (!g_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = EMU_H * 5 / 2;   // FB_H = 600
        int dw = (w * 600) / h;
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)snes_rgb32,
                                  (uint32_t)w, (uint32_t)h, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_hw_scaler_active = 1;
            extern void emulog_field(const char* tag, const char* field, const char* fmt, ...);
            emulog_field("SNES", "scaler", "DE2 GSU 32bpp %dx%d->%dx%d", w, h, dw, dh);
        } else {
            extern void emulog_field(const char* tag, const char* field, const char* fmt, ...);
            emulog_field("SNES", "scaler", "fallback CPU emu_scale");
        }
    }
    if (g_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно w×h (строки по w слов):
        // DE2 PITCH = INSIZE, читает ровно w слов на строку (урок r1495).
        // Источник — EMU_FB (страйд 320), куда snes_run_frame уже скопировал кадр.
        const uint16_t* s = (const uint16_t*)EMU_FB;
        uint32_t* d = snes_rgb32;
        for (int y = 0; y < h; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * w;
            for (int x = 0; x < w; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)snes_rgb32,
                        (uint32_t)((size_t)w * h * 4u));
    }
}

// ---- стоп ----
extern "C" void snes_stop(void) {
    if (!g_loaded) return;
    S9xDeleteCheats();
    Del7110Gfx();
    S9xDeinitGFX();
    S9xDeinitDisplay();
    S9xDeinitAPU();
    S9xDeinitMemory();
    g_loaded = 0;
}

// ---- точка входа для emu.c ----
extern "C" void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("SNES");
    emulog_field("SNES", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    snd_manifest("snes", "spc700 dsp");
    if (snes_init_game(rom, size) != 1) {
        printf("SNES: init failed\n");
        emulog_field("SNES", "init", "FAILED");
        emulog_end("SNES");
        return;
    }
    emulog_field("SNES", "video", "%dx%d", 256, (int)(Settings.PAL ? 239 : 224));
    emulog_field("SNES", "machine", "%s", Settings.PAL ? "PAL" : "NTSC");
    emulog_field("SNES", "init", "OK");
    emu_set_border_color(0x000B0C18);   // тёмно-синеватый (SNES)
    // r1181: Snes9x сам определяет регион по заголовку (Settings.PAL); звук уже
    // считался от 50 Гц, а throttle держал 60 — PAL-игры шли быстрее и картинка
    // рассинхронивалась со звуком. Период кадра — по региону.
    emu_set_period_us(Settings.PAL ? 20000u : 16667u);
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        snes_run_frame();
        snes_render_frame();
        emu_throttle();
        // r1498: при активном DE2 GSU кадр уже на экране (snes_render_frame
        // конвертирует RGB565→XRGB8888, GSU растягивает) — программный emu_scale
        // не нужен и наложился бы поверх аппаратного.
        if (!snes_hw_scaler_active())
            emu_scale(g_eff_w > 0 ? g_eff_w : 256,
                      IPPU.RenderedScreenHeight > 0 ? IPPU.RenderedScreenHeight : 224);
        fb_flush();
        // ESC — удержание ~0.9 с на выход
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1498: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр SNES и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    snes_stop();
    emu_audio_stop();   // r939: НЕ вызывался — кольцо не сбрасывалось после
                        // выхода (доезды/«звук ниоткуда»); теперь чистый сброс
                        // + честный AUDIO-END для диагностики.
    fb_clear(); fb_flush();
    emulog_end("SNES");
}