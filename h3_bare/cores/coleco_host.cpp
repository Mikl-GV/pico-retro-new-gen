// coleco_host.cpp — host-слой Gearcoleco (ColecoVision) для bare-metal H3.
//
// Жизненный цикл (как другие host-слои):
//   coleco_init_game(rom, size) — Init(RGB565) + LoadROMFromBuffer
//   coleco_run_frame()          — RunToVBlank(EMU_FB, NULL, NULL): ядро само
//                                 рисует в EMU_FB (256x192)
//   ввод: USB-клавиатура (ремап) + Sega-геймпад -> KeyPressed
// Выход: ESC (удержание ~0.9 с), как в NES/SNES.
//
// Формат кадра: GC_PIXEL_RGB565 (256x192) -> EMU_FB напрямую.
// Звук не используется (pSampleBuffer=NULL).
#include <stdint.h>
#include <string.h>

#include "gearcoleco/src/definitions.h"
#include "gearcoleco/src/GearcolecoCore.h"

extern "C" {
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "emu.h"
#include "h3_hs_timer.h"
}

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240
#define COL_W   256
#define COL_H   192

static GearcolecoCore* g_core = NULL;
static int g_loaded = 0;

// ---- ввод: USB-клавиатура + Sega-геймпад -> кнопки ColecoVision ----
// Кнопки Coleco: D-Pad + левая/правая кнопка (Fire 1/2) + Keypad 0-9 * #.
// Sega-геймпад: крестовина = D-Pad, A = кнопка 1 (левая), B = кнопка 2
// (правая), Start = кнопка паузы (здесь отдаём в keypad 8 - меню игры),
// Mode = * (пауза в некоторых играх).
static void coleco_build_input(GearcolecoCore* core) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);

    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) core->KeyPressed(Controller_1, Key_Up);
    if (sp & 0x0002) core->KeyPressed(Controller_1, Key_Down);
    if (sp & 0x0004) core->KeyPressed(Controller_1, Key_Left);
    if (sp & 0x0008) core->KeyPressed(Controller_1, Key_Right);
    if (sp & 0x0010) core->KeyPressed(Controller_1, Key_Left_Button);   // A -> Fire 1
    if (sp & 0x0020) core->KeyPressed(Controller_1, Key_Right_Button);  // B -> Fire 2
    if (sp & 0x0080) core->KeyPressed(Controller_1, Keypad_8);          // Start -> 8
    if (sp & 0x0800) core->KeyPressed(Controller_1, Keypad_Hash);          // Mode -> #

    // Клавиатура -> Coleco (ремап REMAP_PLAT_COLECO)
    // Дедолт: стрелки = D-Pad, Z = Fire1, X = Fire2, Enter = Start(8), S = #;
    // цифры 0-9 = клавиатура (набирают на тачпаде Coleco)
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_UP, keys, n))    core->KeyPressed(Controller_1, Key_Up);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_DOWN, keys, n))  core->KeyPressed(Controller_1, Key_Down);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_LEFT, keys, n))  core->KeyPressed(Controller_1, Key_Left);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_RIGHT, keys, n)) core->KeyPressed(Controller_1, Key_Right);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_A, keys, n))     core->KeyPressed(Controller_1, Key_Left_Button);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_B, keys, n))     core->KeyPressed(Controller_1, Key_Right_Button);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_START, keys, n)) core->KeyPressed(Controller_1, Keypad_8);
    if (remap_kbd_pressed(REMAP_PLAT_COLECO, BTN_SELECT, keys, n)) core->KeyPressed(Controller_1, Keypad_Hash);
}

extern "C" int coleco_init_game(const uint8_t* rom, uint32_t size) {
    printf("Gearcoleco: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    if (!rom || size == 0) { printf("Gearcoleco: no ROM\n"); return 0; }

    gb_heap_reset();

    g_core = new GearcolecoCore();
    g_core->Init(GC_PIXEL_RGB565);

    if (!g_core->LoadROMFromBuffer(rom, (int)size, NULL)) {
        printf("Gearcoleco: LoadROMFromBuffer failed\n");
        delete g_core; g_core = NULL;
        return 0;
    }

    // Если ROM не распознан (нет заголовка 0xAA55, CRC не в базе) —
    // форсируем ColecoVision + NTSC через GetCartridge + ResetROM.
    // LoadROMFromBuffer с path=NULL игнорирует config, поэтому делаем это
    // отдельно. Иначе ядро упадёт с Data Abort (невалидный маппер).
    if (!g_core->IsReady()) {
        printf("Gearcoleco: not recognized, forcing ColecoVision\n");
        Cartridge::ForceConfiguration cfg;
        cfg.type = Cartridge::CartridgeColecoVision;
        cfg.region = Cartridge::CartridgeNTSC;
        g_core->GetCartridge()->ForceConfig(cfg);
        g_core->ResetROM(&cfg);
    }

    if (!g_core->IsReady()) {
        printf("Gearcoleco: still not ready\n");
        delete g_core; g_core = NULL;
        return 0;
    }

    g_loaded = 1;
    printf("Gearcoleco: loaded %u bytes\n", (unsigned)size);
    return 1;
}

extern "C" void coleco_run_frame(void) {
    if (!g_loaded || !g_core) return;

    coleco_build_input(g_core);

    // Один кадр: ядро рисует в pFrameBuffer (EMU_FB 256x192) и возвращается
    // после VBlank. Звук отключён: pSampleBuffer=NULL (ядро не рендерит audio)
    g_core->RunToVBlank((u8*)EMU_FB, NULL, NULL);
}

extern "C" void coleco_stop(void) {
    if (g_core) { delete g_core; g_core = NULL; }
    g_loaded = 0;
    printf("Gearcoleco: stopped\n");
}

// ---- точка входа из emu.c ----
extern "C" void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (coleco_init_game(rom, size) != 1) {
        printf("ColecoVision: init failed\n");
        return;
    }
    emu_set_border_color(0x00081814);   // тёмно-оливковый (картридж Coleco)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        coleco_run_frame();
        emu_throttle();
        emu_scale(COL_W, COL_H);
        fb_flush();
        // ESC — удержание ~0.9 с на выход (как NES/SNES)
        if (emu_esc_hold()) goto exit;
    }
exit:
    coleco_stop();
    fb_clear(); fb_flush();
}