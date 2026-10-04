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

// r592 (Д-57): полифазный ресемпл 32768→48000 вместо линейной интерполяции.
// Линейный 16.16 джиттерил фазу (44739 ≠ точное 32768/48000) → «треск» на
// периодических сигналах. Полифазный фильтр (sinc + окно Blackman, 64 фазы,
// 8 тапов, Q15, сумма фазы=1) даёт чистую интерполяцию без накопления ошибки
// фазы — убирает «песок/кашу» на синтезированном (wave/PSG) звуке GBA.
#define GBA_POLY_PHASES 64
#define GBA_POLY_TAPS 8
static const int16_t gba_poly_h[GBA_POLY_PHASES][GBA_POLY_TAPS] = {
  {     0,  -281,  2376, 14288, 14288,  2376,  -281,     0 },
  {     0,  -286,  2293, 14192, 14384,  2460,  -276,     0 },
  {     0,  -291,  2211, 14095, 14478,  2544,  -270,     0 },
  {     0,  -295,  2130, 13997, 14572,  2628,  -265,     0 },
  {     0,  -300,  2049, 13898, 14665,  2713,  -259,     0 },
  {     0,  -304,  1968, 13799, 14757,  2799,  -253,     0 },
  {     0,  -309,  1887, 13699, 14849,  2885,  -247,     0 },
  {     0,  -313,  1806, 13599, 14940,  2971,  -240,     0 },
  {     0,  -317,  1725, 13498, 15030,  3058,  -234,     0 },
  {     0,  -321,  1644, 13397, 15120,  3145,  -228,     0 },
  {     0,  -325,  1564, 13295, 15209,  3232,  -222,     0 },
  {     0,  -329,  1484, 13193, 15297,  3320,  -216,     0 },
  {     0,  -332,  1404, 13091, 15384,  3408,  -210,     0 },
  {     0,  -335,  1325, 12988, 15471,  3496,  -204,     0 },
  {     0,  -339,  1246, 12885, 15557,  3584,  -198,     0 },
  {     0,  -341,  1168, 12782, 15642,  3673,  -192,     0 },
  {     0,  -344,  1090, 12678, 15726,  3762,  -186,     0 },
  {     0,  -347,  1013, 12574, 15810,  3851,  -180,     0 },
  {     0,  -349,   936, 12469, 15893,  3940,  -174,     0 },
  {     0,  -351,   860, 12364, 15975,  4029,  -168,     0 },
  {     0,  -353,   784, 12259, 16056,  4119,  -162,     0 },
  {     0,  -355,   709, 12153, 16137,  4209,  -156,     0 },
  {     0,  -356,   635, 12047, 16217,  4299,  -150,     0 },
  {     0,  -358,   561, 11941, 16296,  4389,  -144,     0 },
  {     0,  -359,   488, 11835, 16374,  4479,  -139,     0 },
  {     0,  -360,   416, 11728, 16452,  4570,  -133,     0 },
  {     0,  -360,   344, 11621, 16529,  4661,  -127,     0 },
  {     0,  -361,   273, 11514, 16605,  4752,  -121,     0 },
  {     0,  -361,   203, 11406, 16681,  4843,  -116,     0 },
  {     0,  -361,   134, 11298, 16756,  4934,  -110,     0 },
  {     0,  -361,    65, 11190, 16830,  5026,  -105,     0 },
  {     0,  -361,    -3, 11082, 16903,  5118,   -99,     0 },
  {     0,  -360,   -71, 10973, 16976,  5210,   -94,     0 },
  {     0,  -360,  -138, 10864, 17048,  5302,   -89,     0 },
  {     0,  -359,  -204, 10755, 17119,  5394,   -84,     0 },
  {     0,  -358,  -269, 10646, 17190,  5487,   -79,     0 },
  {     0,  -357,  -334, 10537, 17260,  5580,   -74,     0 },
  {     0,  -356,  -398, 10427, 17329,  5672,   -69,     0 },
  {     0,  -355,  -461, 10318, 17398,  5765,   -64,     0 },
  {     0,  -353,  -523, 10208, 17466,  5858,   -60,     0 },
  {     0,  -352,  -584,  9999, 17533,  5951,   -55,     0 },
  {     0,  -350,  -645,  9889, 17600,  6044,   -50,     0 },
  {     0,  -348,  -704,  9780, 17666,  6137,   -46,     0 },
  {     0,  -346,  -762,  9670, 17732,  6230,   -41,     0 },
  {     0,  -344,  -819,  9561, 17797,  6323,   -37,     0 },
  {     0,  -342,  -875,  9451, 17862,  6417,   -33,     0 },
  {     0,  -339,  -930,  9341, 17926,  6510,   -29,     0 },
  {     0,  -337,  -984,  9232, 17989,  6604,   -25,     0 },
  {     0,  -334, -1037,  9122, 18052,  6697,   -21,     0 },
  {     0,  -332, -1089,  9013, 18115,  6791,   -18,     0 },
  {     0,  -329, -1139,  8904, 18177,  6884,   -14,     0 },
  {     0,  -326, -1189,  8795, 18238,  6978,   -11,     0 },
  {     0,  -323, -1237,  8687, 18300,  7071,    -8,     0 },
  {     0,  -320, -1284,  8578, 18360,  7165,    -5,     0 },
  {     0,  -317, -1330,  8470, 18421,  7259,    -2,     0 },
  {     0,  -314, -1375,  8362, 18481,  7352,     0,     0 },
  {     0,  -311, -1418,  8254, 18540,  7446,     2,     0 },
  {     0,  -308, -1460,  8147, 18600,  7539,     4,     0 },
  {     0,  -305, -1501,  8040, 18659,  7633,     6,     0 },
  {     0,  -302, -1541,  7933, 18717,  7726,     8,     0 },
  {     0,  -299, -1580,  7827, 18776,  7820,     9,     0 },
  {     0,  -296, -1618,  7721, 18834,  7913,    10,     0 },
  {     0,  -293, -1654,  7615, 18892,  8006,    11,     0 }
};

static u32 gba_resample(const s16* in, u32 nin_pairs, s16* out) {
    u32 ph = gba_rs_phase;
    u32 o = 0;
    while (o < GBA_RS_MAX_OUT) {
        u32 i = ph >> 16;
        if (i + GBA_POLY_TAPS / 2 >= nin_pairs) break;   // нужно 4 сэмпла вперёд
        u32 f = ph & 0xFFFFu;
        // полифазная свёртка 8 тапов вокруг i: in[i-3 .. i+4]
        u32 p = (f * GBA_POLY_PHASES) >> 16;
        if (p >= GBA_POLY_PHASES) p = GBA_POLY_PHASES - 1;
        const int16_t* h = gba_poly_h[p];
        s64 al = 0, ar = 0;
        for (int k = 0; k < GBA_POLY_TAPS; k++) {
            int idx = (int)i - (GBA_POLY_TAPS / 2 - 1) + k;   // i-3..i+4
            if (idx < 0) idx = 0;                              // границы — дублируем край
            if ((u32)idx >= nin_pairs) idx = (int)nin_pairs - 1;
            al += (s64)in[2 * idx]     * h[k];
            ar += (s64)in[2 * idx + 1] * h[k];
        }
        // Q15: вес × 2^-15; сумма до 8×32768×32768 ≈ 8.6e9 → s64, >>15 → s32
        out[2 * o]     = (s16)(s32)(al >> 15);
        out[2 * o + 1] = (s16)(s32)(ar >> 15);
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
boot_mode selected_boot_mode = boot_bios;   // r565: старт через вшитый BIOS
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
    i2s_dc_shift_set(6);   // r591: GBA — ~120 Гц (было 5=240 Гц — резало низ, «провал баса»; щелчки на пачках уходят мягким лимитером ниже)

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
    // r565: boot_bios (а не boot_game). Стеночный лог RnR: pc=0, vc=0,
    // disp=0 — игра стартует как с реального GBA: через BIOS (SWI/VBlank
    // сервисы). boot_game (прямо на 0x08000000 без BIOS-старта) оставлял
    // игры, полагающиеся на BIOS-инициализацию, в висящем состоянии.
    selected_boot_mode = boot_bios;

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

static u32 g_gba_pairs = 0;

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
    // r592 (Д-57): гейн ×1.5 из r591 УБРАН — он перегружал микс (DS ±32768 +
    // PSG ±8192, жёсткий кламп → «очень громко/треск», на 5% как 50% Lynx).
    // Полифазный ресемпл сохраняет уровень (сумма тапов фазы = 1) — гейн не
    // нужен. Если по стенду GBA окажется тише Lynx — отдельная дельта с
    // мягким лимитером, не хард-клампом.
    for (u32 i = 0; i < np; i++)
        i2s_push_sample(rsbuf[2 * i], rsbuf[2 * i + 1]);
    g_gba_pairs = np;   // r645: для периода кадра (вместо магического адреса I2S)
}

u32 gba_last_pairs(void) { return g_gba_pairs; }

void gba_render_frame(void) {
    if (!g_loaded) return;
    if (!gba_screen_pixels) return;
    for (int y = 0; y < GBA_SCREEN_HEIGHT && y < EMU_H; y++)
        for (int x = 0; x < GBA_SCREEN_WIDTH && x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = gba_screen_pixels[y * GBA_SCREEN_PITCH + x];
}