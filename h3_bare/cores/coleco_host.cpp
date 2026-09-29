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
#include "gearcoleco/src/Memory.h"

extern "C" {
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "emu.h"
#include "h3_hs_timer.h"
#include "fat.h"
}

// r0.286: глошим логи Gearcoleco — Log/Error (log.h Log_func) в нашей сборке
// (не libretro, не DEBUG) всегда печатали в UART и заливали лог в работе.
bool g_mcp_stdio_mode = true;

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240
#define COL_W   256
#define COL_H   192

static GearcolecoCore* g_core = NULL;
static int g_loaded = 0;

// r0.194: ядро (TMS9918A::Render16bit) пишет кадр 256×192 ПОДРЯД (pitch 256),
// а EMU_FB имеет pitch 320 — прямые строки ложились со сдвигом и «дублировались».
// Рендерим в свой буфер, потом построчно в EMU_FB.
static uint16_t g_col_fb[COL_W * COL_H] __attribute__((aligned(8)));

// ---- ввод: USB-клавиатура + Sega-геймпад -> кнопки ColecoVision ----
// Кнопки Coleco: D-Pad + левая/правая кнопка (Fire 1/2) + Keypad 0-9 * #.
// Sega-геймпад: крестовина = D-Pad, A = кнопка 1 (левая), B = кнопка 2
// (правая), Start = кнопка паузы (здесь отдаём в keypad 8 - меню игры),
// Mode = * (пауза в некоторых играх).
static void coleco_build_input(GearcolecoCore* core) {
    // r0.280: usb_kbd_get_raw (кэш последнего отчёта) вместо get_raw: get_raw
// обнуляет клавиши через ~100 мс без свежих boot-отчётов, а многие клавиши
// не шлют отчёты на удержание → удержание «рвалось» (движение по шагу).
uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t sp = sega_pad_scan();

    // r176: для каждой кнопки зовём KeyPressed ИЛИ KeyReleased по факту —
    // раньше был только KeyPressed, и кнопки в Gearcoleco «залипали».
    int plat = REMAP_PLAT_COLECO;
    struct { bool on; GC_Keys key; } btns[] = {
        { (sp & 0x0001) || remap_kbd_pressed(plat, BTN_UP, keys, n),    Key_Up },
        { (sp & 0x0002) || remap_kbd_pressed(plat, BTN_DOWN, keys, n),  Key_Down },
        { (sp & 0x0004) || remap_kbd_pressed(plat, BTN_LEFT, keys, n),  Key_Left },
        { (sp & 0x0008) || remap_kbd_pressed(plat, BTN_RIGHT, keys, n), Key_Right },
        { (sp & 0x0010) || remap_kbd_pressed(plat, BTN_A, keys, n),     Key_Left_Button },
        { (sp & 0x0020) || remap_kbd_pressed(plat, BTN_B, keys, n),     Key_Right_Button },
        { (sp & 0x0080) || remap_kbd_pressed(plat, BTN_START, keys, n), Keypad_8 },
        { (sp & 0x0800) || remap_kbd_pressed(plat, BTN_SELECT, keys, n), Keypad_Hash },
    };
    for (size_t i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
        if (btns[i].on) core->KeyPressed(Controller_1, btns[i].key);
        else            core->KeyReleased(Controller_1, btns[i].key);
    }

    // r0.195: клавиатура → keypad Coleco полностью (кнопок на клаве много):
    // цифры 1..9,0 = Keypad_1..Keypad_0, Q = '*', W = '#'. Остальное (D-Pad,
    // Fire1/2, Enter=Keypad_8, S=Keypad_Hash) — выше через remap_btns.
    static const struct { uint8_t sc; GC_Keys key; } kbd_keypad[] = {
        {30, Keypad_1}, {31, Keypad_2}, {32, Keypad_3}, {33, Keypad_4},
        {34, Keypad_5}, {35, Keypad_6}, {36, Keypad_7}, {37, Keypad_8},
        {38, Keypad_9}, {39, Keypad_0},
        {20, Keypad_Asterisk},   // Q = *
        {26, Keypad_Hash},       // W = #
    };
    for (size_t i = 0; i < sizeof(kbd_keypad)/sizeof(kbd_keypad[0]); i++) {
        int on = 0;
        for (int k = 0; k < n; k++)
            if (keys[k] == kbd_keypad[i].sc) { on = 1; break; }
        if (on) core->KeyPressed(Controller_1, kbd_keypad[i].key);
        else    core->KeyReleased(Controller_1, kbd_keypad[i].key);
    }
}

extern "C" int coleco_init_game(const uint8_t* rom, uint32_t size) {
    printf("Coleco: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    if (!rom || size == 0) { printf("Coleco: no ROM\n"); return 0; }

    gb_heap_reset();

    g_core = new GearcolecoCore();
    g_core->Init(GC_PIXEL_RGB565);

    // r178: OS-7 BIOS вшит в прошивку (bios_data.S, 8 КБ, CRC32 0x3AA93EF3).
    // r0.186: порядок загрузки — как в эталоне (platforms/libretro/libretro.cpp,
    // load_colecovision_firmware): СНАЧАЛА BIOS в Memory::LoadBiosFromBuffer()
    // (без него IsBiosLoaded()=false, машина никогда не «ready», наш fallback
    // ResetROM крутится в неконсистентном состоянии), ЗАТЕМ тот же образ в
    // Adam (LoadAdamFirmware). Раньше мы делали только второе.
    extern unsigned char coleco_bios_data[];
    int bios_ok = g_core->GetMemory()->LoadBiosFromBuffer(coleco_bios_data, 0x2000);
    if (bios_ok)
        g_core->LoadAdamFirmware(GC_ADAM_FIRMWARE_OS7, coleco_bios_data, 0x2000);

    // fallback: если вшитый не завёлся — ищем BIOS на SD в корне.
    if (!bios_ok) {
        static uint8_t coleco_bios[0x2000];
        const char* const bios_names[] = { "coleco.rom", "colecovision.rom", "os7.u2" };
        for (int bi = 0; bi < 3 && !bios_ok; bi++) {
            fat_entry_t f;
            if (!fat_find("/", bios_names[bi], &f)) continue;
            if (f.size != 0x2000) continue;   // жёсткий размер 8 КБ
            if (fat_read_file(&f, 0, coleco_bios, 0x2000) != 0x2000) continue;
            if (g_core->GetMemory()->LoadBiosFromBuffer(coleco_bios, 0x2000)) {
                g_core->LoadAdamFirmware(GC_ADAM_FIRMWARE_OS7, coleco_bios, 0x2000);
                bios_ok = 1;
            }
        }
    }
    if (!bios_ok)
        printf("Coleco: WARNING OS-7 BIOS not loaded (builtin+SD)\n");

    if (!g_core->LoadROMFromBuffer(rom, (int)size, NULL)) {
        printf("Coleco: LoadROMFromBuffer failed\n");
        delete g_core; g_core = NULL;
        return 0;
    }

    // Если ROM не распознан (нет заголовка 0xAA55, CRC не в базе) —
    // форсируем ColecoVision + NTSC через GetCartridge + ResetROM.
    // LoadROMFromBuffer с path=NULL игнорирует config, поэтому делаем это
    // отдельно. Иначе ядро упадёт с Data Abort (невалидный маппер).
    if (!g_core->IsReady()) {
        printf("Coleco: not recognized, forcing ColecoVision\n");
        Cartridge::ForceConfiguration cfg;
        cfg.type = Cartridge::CartridgeColecoVision;
        cfg.region = Cartridge::CartridgeNTSC;
        g_core->GetCartridge()->ForceConfig(cfg);
        g_core->ResetROM(&cfg);
    }

    if (!g_core->IsReady()) {
        printf("Coleco: still not ready\n");
        delete g_core; g_core = NULL;
        return 0;
    }

    g_loaded = 1;
    printf("Coleco: loaded %u bytes\n", (unsigned)size);
    return 1;
}

extern "C" void coleco_run_frame(void) {
    if (!g_loaded || !g_core) return;

    coleco_build_input(g_core);

    // Один кадр: ядро рисует в g_col_fb (256x192, pitch 256) и возвращается
    // после VBlank. Звук отключён: pSampleBuffer=NULL (ядро не рендерит audio)
    g_core->RunToVBlank((u8*)g_col_fb, NULL, NULL);

    // r0.194: перенос 256×192 в EMU_FB с pitch 320 построчно — иначе картинка
    // «дублируется со сдвигом» (строки ложились подряд, без учёта ширины FB).
    for (int y = 0; y < COL_H; y++)
        memcpy(EMU_FB + y * EMU_W, g_col_fb + y * COL_W, COL_W * 2);
}

extern "C" void coleco_stop(void) {
    if (g_core) { delete g_core; g_core = NULL; }
    g_loaded = 0;
    printf("Coleco: stopped\n");
}

// ---- точка входа из emu.c ----
extern "C" void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    emu_prepare();
    memset(g_col_fb, 0, sizeof(g_col_fb));   // свой кадр-буфер не чистится emu_prepare
    if (coleco_init_game(rom, size) != 1) {
        printf("Coleco: init failed\n");
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