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
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"

// r521: ресемпл звука gpSP 65536 → 48000 Гц (линейная интерполяция, 16.16).
// I2S остаётся на 48 кГц — честных делителей под 65536 у H3 нет (ср.: ровный
// 48 кГц это PLL/8; для 65536 пришлось бы дробить LRCKPER/PLL — риск мимо
// частоты). Отношение 65536/48000 = 4/3: фаза 16.16, шаг по входным парам.
// r524/r525: ядро на 32768 Гц. ВАЖНО: 32768→48000 — это УВЕЛИЧЕНИЕ
// ×1.4648: 549 входных пар дают ~804 выходных (16.9 мс вывода) — никак не
// 402 (r524-комментарий был ошибочен, cap 460 резал звук → «грязно»).
#define GBA_RS_INC 44739u   // (32768<<16)/48000
#define GBA_RS_MAX_OUT 900  // норма ~804, cap выше — не режем нормаль
static u32 gba_rs_phase = 0;   // непрерывна между кадрами (поток не рвётся)

static u32 gba_resample(const s16* in, u32 nin_pairs, s16* out) {
    u32 ph = gba_rs_phase;
    u32 o = 0;
    while (o < GBA_RS_MAX_OUT) {
        u32 i = ph >> 16;
        if (i + 1 >= nin_pairs) break;
        u32 f = ph & 0xFFFFu;
        int l0 = in[2 * i],     l1 = in[2 * (i + 1)];
        int r0 = in[2 * i + 1], r1 = in[2 * (i + 1) + 1];
        // r542: интерполяция в int64 — (l1-l0)*f до ±4.3e9 было signed UB;
        // результат всё равно обрезается в s16 как и раньше.
        out[2 * o]     = (s16)(l0 + (s32)(((s64)(l1 - l0) * (s32)f) >> 16));
        out[2 * o + 1] = (s16)(r0 + (s32)(((s64)(r1 - r0) * (s32)f) >> 16));
        o++;
        ph += GBA_RS_INC;
    }
    // r523: r522 вычитал nin<<16 даже когда ph меньше — фаза уходила в
    // огромный unsigned (i=65534) и ресемплер выдавал 0 навсегда.
    if (ph >= ((u64)nin_pairs << 16))
        gba_rs_phase = (u32)(ph - ((u64)nin_pairs << 16));
    else
        gba_rs_phase = 0;
    return o;
}

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

    uint16_t sp = pad_scan_combined();
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
    gba_rs_phase = 0;   // r544: фаза ресемплера не должна нестись между играми

    // ROM из памяти: host-глобалы ядра (без filestream и без аллокации пула)
    g_ram_rom = rom;
    g_ram_rom_size = size;

    // Экранный буфер ядра — статический (без malloc)
    gba_screen_pixels = g_gba_screen;
    memset(g_gba_screen, 0, sizeof(g_gba_screen));

    // BIOS ВШИТ В ПРОШИВКУ (16 КБ, open-source, bios_data.S -> .incbin).
    // r538 (F1): загрузка «официального» BIOS с SD УБРАНА по решению
    // владельца — BIOS всегда берётся из образа прошивки, никакой
    // зависимости от файлов на SD (предсказуемый старт, лёгкий клон).
    memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
    printf("GBA: BIOS embedded (open-source, 16K)\n");
    selected_boot_mode = boot_game;

    // Грузим ROM (load_gamepak_raw увидит g_ram_rom и замапит напрямую)
    if (load_gamepak(NULL, NULL, FEAT_AUTODETECT, FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0) {
        printf("GBA: load_gamepak failed\n");
        return 0;
    }

    // r524: выводимый звук ядра — 32768 Гц (не 65536), чтобы пары/кадр
    // вдвое меньшие (~549) влезали в время кадра. Ставим ДО init_sound():
    // init_sound() теперь считает tick_step от фактической sound_frequency.
    extern u32 sound_frequency;
    extern u32 sound_freq_bits;
    sound_frequency = 32768;
    sound_freq_bits = 15;

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

    // Звук (r521/r524): gpSP отдаёт 32768 Гц стерео s16 — ресемплим в 48000
    // (~804 пары/кадр, влезает в кадр с запасом) и пушим в I2S.
    static s16 sndbuf[4096];
    static s16 rsbuf[GBA_RS_MAX_OUT * 2];
    u32 frames = sound_read_samples(sndbuf, 549);  // возвращает пары ~549/кадр
    u32 np = gba_resample(sndbuf, frames, rsbuf);
    for (u32 i = 0; i < np; i++)
        i2s_push_sample(rsbuf[2 * i], rsbuf[2 * i + 1]);
}

void gba_render_frame(void) {
    if (!g_loaded) return;
    if (!gba_screen_pixels) return;
    for (int y = 0; y < GBA_SCREEN_HEIGHT && y < EMU_H; y++)
        for (int x = 0; x < GBA_SCREEN_WIDTH && x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = gba_screen_pixels[y * GBA_SCREEN_PITCH + x];
}