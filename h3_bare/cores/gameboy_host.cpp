// gameboy_host.cpp — host-слой Game Boy / Game Boy Color (binjgb) для H3.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "cheatdb.h"
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

// ---- Game Genie (Game Boy) чит-коды ----
// Формат: "XX-XXX-XXX" или "XX-XXX-XXX-YY" (дефисы необязательны).
// Алгоритм декодера (проверенный, из Gearboy):
//   value  = hex(code[0..1])
//   address= (hex(code[2])<<8 | hex(code[4])<<4 | hex(code[5]) |
//            (hex(code[6])^0xF)<<12) & 0x7FFF
//   compare= ((hex(code[8])<<4 | hex(code[10])) ^ 0xFF) (если есть доп. пара)
//            compare = ((compare>>2 | compare<<6) ^ 0x45) & 0xFF
// Патчим ROM напрямую (binjgb читает ROM через cart_info->data из ROM_BUF).
static int gb_hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void gb_apply_cheats(uint8_t* rom, uint32_t size) {
    int cnt = cheats_count();
    for (int i = 0; i < cnt; i++) {
        if (!cheats_enabled(i)) continue;
        const char* code = cheats_code(i);
        if (!code) continue;
        // убираем дефисы
        char c[16];
        int cl = 0;
        for (const char* p = code; *p && cl < 15; p++)
            if (*p != '-' && *p != ' ') c[cl++] = *p;
        c[cl] = 0;
        if (cl < 7) { printf("[GB] cheat: bad '%s'\n", code); continue; }

        int h0 = gb_hexv(c[0]), h1 = gb_hexv(c[1]);
        int h2 = gb_hexv(c[2]), h4 = gb_hexv(c[4]), h5 = gb_hexv(c[5]), h6 = gb_hexv(c[6]);
        if (h0 < 0 || h1 < 0 || h2 < 0 || h4 < 0 || h5 < 0 || h6 < 0) {
            printf("[GB] cheat: bad '%s'\n", code); continue;
        }
        uint8_t value  = (uint8_t)((h0 << 4) | h1);
        uint32_t addr  = (uint32_t)((h2 << 8) | (h4 << 4) | h5 | ((h6 ^ 0xF) << 12)) & 0x7FFF;
        int has_compare = 0;
        uint8_t compare = 0;
        if (cl >= 9) {
            int h8 = gb_hexv(c[8]), h10 = gb_hexv(c[10]);
            if (cl >= 11 && h8 >= 0 && h10 >= 0) {
                compare = (uint8_t)((h8 << 4) | h10);
                compare = (uint8_t)((((compare >> 2) | (compare << 6)) ^ 0x45) & 0xFF);
                compare ^= 0xFF;
                has_compare = 1;
            }
        }

        // применяем ко всем банкам ROM (как Gearboy)
        int banks = (size + 0x3FFF) / 0x4000;
        for (int bank = 0; bank < banks; bank++) {
            uint32_t bank_addr = (uint32_t)bank * 0x4000 + (addr & 0x3FFF);
            if (bank_addr >= size) break;
            if (!has_compare || rom[bank_addr] == compare)
                rom[bank_addr] = value;
        }
    }
}

extern "C" int gb_init_game(const uint8_t* rom, uint32_t size) {
    gb_heap_reset();

    EmulatorInit init;
    memset(&init, 0, sizeof(init));
    init.rom.data = (uint8_t*)rom;  // ROM напрямую, без копирования
    init.rom.size = size;
    init.audio_frequency = 48000;
    init.audio_frames = 1024;
    init.random_seed = 42;
    init.force_dmg = FALSE;
    init.cgb_color_curve = CGB_COLOR_CURVE_NONE;

    g_emu = emulator_new(&init);
    if (!g_emu) { printf("[GB] emulator_new failed\n"); return 0; }

    // Применяем отмеченные читы (GB Game Genie) — патчим ROM напрямую
    gb_apply_cheats((uint8_t*)rom, size);

    printf("[GB] init ok, size=%u\n", (unsigned)size);
    return 1;
}

extern "C" void gb_run_frame(void) {
    if (!g_emu) return;

    // Ввод: USB-клавиатура + Sega-геймпад -> Game Boy кнопки
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    JoypadButtons jp;
    memset(&jp, 0, sizeof(jp));

    // Sega-геймпад: A->A B->B Start Mode->Select
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) jp.up = TRUE;
    if (sp & 0x0002) jp.down = TRUE;
    if (sp & 0x0004) jp.left = TRUE;
    if (sp & 0x0008) jp.right = TRUE;
    if (sp & 0x0010) jp.A = TRUE;        // Sega A -> GB A
    if (sp & 0x0020) jp.B = TRUE;        // Sega B -> GB B
    if (sp & 0x0080) jp.start = TRUE;
    if (sp & 0x0800) jp.select = TRUE;   // Mode -> Select

    // Клавиатура — через ремап (Settings → Keyboard remap). Дедолт:
    // стрелки=D-Pad, Z=B X=A S=Select Enter=Start.
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_UP, keys, n))    jp.up = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_DOWN, keys, n))  jp.down = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_LEFT, keys, n))  jp.left = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_RIGHT, keys, n)) jp.right = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_B, keys, n))     jp.B = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_A, keys, n))     jp.A = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_SELECT, keys, n)) jp.select = TRUE;
    if (remap_kbd_pressed(REMAP_PLAT_GB, BTN_START, keys, n)) jp.start = TRUE;
    emulator_set_joypad_buttons(g_emu, &jp);

    EmulatorEvent events;
    int guard = 0;
    // r158: safety-лимит — если binjgb не выставляет NEW_FRAME (зависший ROM
    // или сбой), не крутим вечно: кадр возвращается, и emu_esc_hold вызывается
    // (иначе «не работает выход из эмулятора»).
    do {
        events = emulator_run_until(g_emu, emulator_get_ticks(g_emu) + PPU_FRAME_TICKS);
    } while (!(events & EMULATOR_EVENT_NEW_FRAME) && ++guard < 4);

    // Звук отключён (без I2S): сбрасываем буфер, ядро перезапишет
    AudioBuffer* ab = emulator_get_audio_buffer(g_emu);
    u32 nframes = audio_buffer_get_frames(ab);
    (void)nframes;
    ab->position = ab->data;
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