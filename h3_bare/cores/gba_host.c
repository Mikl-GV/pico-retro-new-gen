// gba_host.c — host-слой Game Boy Advance (gpSP) для H3 bare-metal.
//
// Жизненный цикл (как в других хостах):
//   gba_init_game(rom, size)  — ставит g_ram_rom, грузит ROM, BIOS, reset_gba
//   gba_run_frame()           — один кадр: ввод + execute_arm (до VBlank)
//   gba_render_frame()        — копия gba_screen_pixels (240x160) в EMU_FB
//
// Ядро gpSP собирается без libretro.c: ROM подаётся напрямую из памяти
// (см. g_ram_rom в gba_memory.c), файлов нет, звук заглушен (нет DAC).

#include <stdint.h>
#include <string.h>

#include "gba_sp/common.h"
#include "gba_sp/gba_memory.h"
#include "gba_sp/main.h"
#include "gba_sp/video.h"

#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"

extern int printf(const char* fmt, ...);

// EMU_FB — общий кадровый буфер эмуляторов (320x240 RGB565)
#define EMU_FB ((uint16_t*)0x5F800000)
#define EMU_W  320
#define EMU_H  240

// ---- глобалы, которые в оригинале даёт libretro.c / input.c ----
// main_path определён в gba_sp/main.c — здесь НЕ дублируем.
boot_mode selected_boot_mode = boot_game;
u32 skip_next_frame = 0;
int  sprite_limit = 1;
u32  idle_loop_target_pc = 0xFFFFFFFF;
u32  translation_gate_targets = 0;
u32  translation_gate_target_pc[MAX_TRANSLATION_GATES];
int  dynarec_enable = 0;
u32  num_skipped_frames = 0;

// Экранный буфер ядра (video.cc пишет сюда каждый кадр)
static uint16_t g_gba_screen[GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1)];

// BIOS из bios_data.S (16KB open-source) — объявлен в gba_memory.h
extern u8 bios_rom[1024 * 16];

// ROM из памяти — глобалы ядра (gba_memory.c)
extern const u8 *g_ram_rom;
extern u32 g_ram_rom_size;

static int g_loaded = 0;

// ---- ввод: USB-клавиатура + Sega-геймпад -> кнопки GBA ----
// GBA биты (как в input.h): A=0x01 B=0x02 Select=0x04 Start=0x08
// Right=0x10 Left=0x20 Up=0x40 Down=0x80 L=0x100 R=0x200
static u16 gba_buttons(void) {
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    u16 b = 0;

    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) b |= 0x40;   // Up
    if (sp & 0x0002) b |= 0x80;   // Down
    if (sp & 0x0004) b |= 0x20;   // Left
    if (sp & 0x0008) b |= 0x10;   // Right
    if (sp & 0x0010) b |= 0x01;   // Sega A -> A
    if (sp & 0x0020) b |= 0x02;   // Sega B -> B
    if (sp & 0x0040) b |= 0x100;  // Sega C -> L
    if (sp & 0x0100) b |= 0x200;  // Sega X -> R
    if (sp & 0x0080) b |= 0x08;   // Sega Start -> Start
    if (sp & 0x0800) b |= 0x04;   // Sega Mode -> Select

    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        (void)sc;   // проверка скан-кодов идёт через remap_kbd_pressed (см. ниже)
    }
    // Клавиатура -> кнопки GBA (ремап через Settings → Keyboard remap).
    // Залипания нет: remap_kbd_pressed проверяет массив keys целиком.
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_UP, keys, n))    b |= 0x40;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_DOWN, keys, n))  b |= 0x80;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_LEFT, keys, n))  b |= 0x20;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_RIGHT, keys, n)) b |= 0x10;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_A, keys, n))     b |= 0x01;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_B, keys, n))     b |= 0x02;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_L, keys, n))     b |= 0x100;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_R, keys, n))     b |= 0x200;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_SELECT, keys, n)) b |= 0x04;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_START, keys, n)) b |= 0x08;
    return b;
}

int gba_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) { printf("GBA: no rom\n"); return 0; }
    printf("GBA: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    // ROM из памяти: host-глобалы ядра (без filestream и без аллокации пула)
    g_ram_rom = rom;
    g_ram_rom_size = size;

    // Экранный буфер ядра — статический (без malloc)
    gba_screen_pixels = g_gba_screen;
    memset(g_gba_screen, 0, sizeof(g_gba_screen));

    // Встроенный BIOS (16KB open-source)
    memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
    selected_boot_mode = boot_game;

    // Грузим ROM (load_gamepak_raw увидит g_ram_rom и замапит напрямую)
    if (load_gamepak(NULL, NULL, FEAT_AUTODETECT, FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0) {
        printf("GBA: load_gamepak failed\n");
        return 0;
    }

    init_sound();
    reset_gba();

    g_loaded = 1;
    printf("GBA: loaded %u bytes, size=%u\n", (unsigned)size, (unsigned)gamepak_size);
    return 1;
}

void gba_run_frame(void) {
    if (!g_loaded) return;

    // Ввод -> REG_P1 (активный низ)
    u16 b = gba_buttons();
    write_ioreg(REG_P1, (~b) & 0x3FF);

    // Один кадр: execute_arm исполняет, пока не наберёт кадр
    // (внутри сама вызывает update_gba и завершается по VBlank).
    clear_gamepak_stickybits();
    execute_arm(execute_cycles);

    // Звук отключён: дрейним буфер gpSP, чтобы не переполнить
    static s16 sndbuf[2048];
    u32 frames = sound_read_samples(sndbuf, 1024);
    (void)frames;
}

void gba_render_frame(void) {
    if (!g_loaded) return;
    if (!gba_screen_pixels) return;
    for (int y = 0; y < GBA_SCREEN_HEIGHT && y < EMU_H; y++)
        for (int x = 0; x < GBA_SCREEN_WIDTH && x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = gba_screen_pixels[y * GBA_SCREEN_PITCH + x];
}