// gameboy_host.cpp — host-слой Game Boy / Game Boy Color (binjgb) для H3.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "cheatdb.h"
#include "i2s.h"
#include "h3_hs_timer.h"
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

// r1069/r1070: диагностика «виснет при загрузке» — static на уровне файла,
// чтобы сбрасывать при каждом gb_init_game() (внутри функции не сбросить).
static int gb_warned = 0;
static uint32_t gb_warn_t = 0;

// r1068: binjgb требует, чтобы ROM-буфер был реально кратен 32K и имел
// физическую ёмкость >= размера, заявленного в заголовке (ROM_SIZE code).
// rom_browser грузит файл как есть (может быть обрезан/некратен), а ядро
// в get_cart_infos() при max_file_size > file_size вызывает file_data_resize()
// (в стабах — только fd->size = new_sz, буфер НЕ растёт): cart_info->size
// становится больше настоящего буфера → чтение банков валится за конец →
// виснет/«не принимает». Поэтому host делает СВОЮ копию ROM фиксированной
// ёмкости (до 8 МБ — максимум ROM_8M) и дополняет хвост нулями.
#define GB_HOST_ROM_MAX (8u * 1024u * 1024u)
#define GB_ROM_SIZE_ADDR 0x148u
// Заголовок: ROM size code -> байт (как в binjgb FOREACH_ROM_SIZE).
static const uint32_t gb_rom_size_bytes[9] = {
    32u * 1024, 64u * 1024, 128u * 1024, 256u * 1024, 512u * 1024,
    1024u * 1024, 2u * 1024u * 1024, 4u * 1024u * 1024, 8u * 1024u * 1024,
};
static uint8_t g_gb_rom_copy[GB_HOST_ROM_MAX] __attribute__((aligned(8)));
static uint32_t g_gb_rom_copy_size = 0;

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
    // r904 (B1): старый g_emu живёт в bump-пуле; gb_heap_reset() ниже затирает
    // его память. Обнуляем указатель ДО сброса пула, иначе при отказе нового
    // emulator_new() g_emu остался бы висячим на затёртой памяти, и
    // gb_run_frame()/gb_render_frame() (guard «if (!g_emu) return» на 120/177)
    // пошли бы по мусору пула.
    g_emu = NULL;
    // r1070: сброс внутрифайловой диагностики при каждой загрузке ROM
    // (иначе static-флаг живёт до перезагрузки прошивки).
    gb_warned = 0;
    gb_warn_t = 0;
    gb_heap_reset();
    // r0.417 (M6 аудита): битый элемент (NULL/0) — не запускаем binjgb
    // на пустом буфере (это единственный host без guard'а, как у остальных).
    if (!rom || size == 0) { printf("[GB] no ROM\n"); return 0; }

    // r1068: собственная копия ROM + дополнение нулями до размера,
    // заявленного заголовком (или кратного 32K). Проверяем header 0x148 —
    // валидный ROM size code; если битый (не 0..8) или заголовок меньше
    // 0x149 байт — берём ближайшую 32K-границу >= размера файла.
    uint32_t want = size;
    if (size >= 0x149u) {
        uint8_t code = rom[GB_ROM_SIZE_ADDR];
        if (code <= 8u) {
            uint32_t hdr = gb_rom_size_bytes[code];
            if (hdr > want) want = hdr;
        }
    }
    if ((want & 0x7FFFu) != 0) want = (want + 0x7FFFu) & ~0x7FFFu;
    if (want > GB_HOST_ROM_MAX) want = GB_HOST_ROM_MAX;
    memset(g_gb_rom_copy, 0, want);            // хвост — нули (неиспользуемые банки)
    memcpy(g_gb_rom_copy, rom, size);
    g_gb_rom_copy_size = want;
    rom = g_gb_rom_copy;

    i2s_dc_shift_set(6);   // r943: дефолт 10 (~7.5 Гц) — без среза нижней середины (r941-регрессия)

    EmulatorInit init;
    memset(&init, 0, sizeof(init));
    init.rom.data = (uint8_t*)rom;  // ROM напрямую, без копирования
    init.rom.size = g_gb_rom_copy_size;
    init.audio_frequency = 48000;
    init.audio_frames = 1024;
    init.random_seed = 42;
    init.force_dmg = FALSE;
    init.cgb_color_curve = CGB_COLOR_CURVE_NONE;

    g_emu = emulator_new(&init);
    if (!g_emu) { printf("[GB] emulator_new failed\n"); return 0; }

    // Применяем отмеченные читы (GB Game Genie) — патчим ROM напрямую
    gb_apply_cheats((uint8_t*)rom, g_gb_rom_copy_size);

    printf("[GB] init ok, size=%u\n", (unsigned)g_gb_rom_copy_size);
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
    uint16_t sp = pad_scan_combined();
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
    // r1070: heartbeat вокруг do/while — отличаем «зависло внутри run_until»
    // (TICKS не растёт) от «крутимся без NEW_FRAME» (TICKS растёт).
    Ticks t_before = 0, t_after = 0;
    do {
        t_before = emulator_get_ticks(g_emu);
        events = emulator_run_until(g_emu, emulator_get_ticks(g_emu) + PPU_FRAME_TICKS);
        t_after = emulator_get_ticks(g_emu);
    } while (!(events & EMULATOR_EVENT_NEW_FRAME) && ++guard < 4);
    if (!gb_warned && t_after == t_before) {
        printf("[GB] STALL: ticks not moving (guard=%d event=%u frame=%u)\n",
               guard, (unsigned)events,
               (unsigned)emulator_get_ppu_frame(g_emu));
        gb_warned = 1;
    }

    // r1069: детект без-кадра (первый кадр не пришёл за 4 попытки).
    if (!(events & EMULATOR_EVENT_NEW_FRAME) && !gb_warned) {
        printf("[GB] WARN: no NEW_FRAME after 4 tries (ticks=%u steps=%u frame=%u event=%u)\n",
               (unsigned)emulator_get_ticks(g_emu),
               (unsigned)emulator_get_step_count(g_emu),
               (unsigned)emulator_get_ppu_frame(g_emu),
               (unsigned)events);
    }
    if (!gb_warned && (h3_hs_timer_lo_us() - gb_warn_t > 3000000u)) {
        // Кадр идёт, но мы уже 3 с крутимся — печатаем жив.
        printf("[GB] alive: ticks=%u ppu=%u fill=%u\n",
               (unsigned)emulator_get_ticks(g_emu),
               (unsigned)emulator_get_ppu_frame(g_emu),
               (unsigned)audio_buffer_get_frames(emulator_get_audio_buffer(g_emu)));
        gb_warned = 1;
    }
    // После успешного первого кадра диагностику глушим.
    if (events & EMULATOR_EVENT_NEW_FRAME) gb_warned = 1;

    // Звук GB/GBC на I2S (r596). binjgb отдаёт буфер u8 2-канальный
    // (пары L/R, 0..255, центр 128, 48000 Гц) — см. write_audio_frame
    // в emulator.c (SOUND_OUTPUT_COUNT=2). Раньше здесь была ЗАГЛУШКА:
    // буфер сбрасывался в никуда (ab->position = ab->data), звук ядро
    // синтезировало, но в I2S не уходил.
    AudioBuffer* ab = emulator_get_audio_buffer(g_emu);
    u32 nframes = audio_buffer_get_frames(ab);
    // r1326 (A1): разворачиваем u8-пары в s16 и подаём ОДНИМ i2s_push_block
    // вместо цикла push_sample (dsb на пару).
    static int16_t gb_snd[1024 * 2];
    u32 np = nframes;
    if (np > 1024) np = 1024;
    for (u32 i = 0; i < np; i++) {
        gb_snd[2 * i]     = (int16_t)((int32_t)ab->data[2 * i] - 128) << 8;
        gb_snd[2 * i + 1] = (int16_t)((int32_t)ab->data[2 * i + 1] - 128) << 8;
    }
    if (np) i2s_push_block(gb_snd, np);
    ab->position = ab->data;   // буфер перезапишется ядром («съели»)
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