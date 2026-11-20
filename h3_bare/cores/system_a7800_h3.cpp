// system_a7800_h3.cpp — host-слой ProSystem (Atari 7800) для H3.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "cheatdb.h"
#include "i2s.h"
}

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#include "fb_text.h"
#include "a7800/ProSystem.h"
#include "a7800/Maria.h"
#include "a7800/Memory.h"
#include "a7800/Cartridge.h"
#include "a7800/Region.h"
#include "a7800/Tia.h"
#include "a7800/Pokey.h"

static uint16_t a7_pal_rgb565[256];
static uint8_t a7_rom_data[1024 * 1024];
static uint32_t a7_rom_size = 0;

// ---- r1523: аппаратный DE2 GSU (вместо программного emu_scale) ----
// Кадр Atari 7800 — RGB565 320×240 в EMU_FB (страйд 320) после
// maria_LineReady. В a7800_render_frame кадр конвертируется в XRGB8888
// (a7_rgb32, упакован 320×240 — строки по 320 слов, DE2 PITCH = INSIZE,
// урок r1495), GSU растягивает на всю высоту FB_H=600 с сохранением
// пропорций (dst_w = 320×600/240 = 800, dx = 112 — как col_rgb32 r1507).
// Размер фиксированный — перекалибровка при смене w/h не нужна (урок r1506
// не применим). Сброс флага при входе (урок r1496). Fallback — программный
// emu_scale (g_a7_hw_scaler_active=0). r1519-эталон: вызывается БЕЗУСЛОВНО.
static uint32_t a7_rgb32[EMU_W * EMU_H] __attribute__((aligned(4), section(".noinit_fbn")));
static int g_a7_hw_scaler_active = 0;

// r1523: рендер кадра Atari 7800 на DE2 GSU. К этому моменту maria_LineReady
// уже скопировал кадр RGB565 320×240 в EMU_FB (лево-верх, страйд 320).
// Конвертируем его в XRGB8888 (a7_rgb32, упакован 320×240 — строки по
// 320 слов, PITCH = INSIZE, урок r1495), DE2 GSU растягивает на всю высоту
// FB_H=600 с сохранением пропорций (dst_w = 320×600/240 = 800, dx = 112 по
// центру). Fallback — программный emu_scale из самого EMU_FB
// (g_a7_hw_scaler_active=0).
extern "C" void a7800_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    if (!g_a7_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (EMU_W * 600) / EMU_H;   // 320×600/240 = 800
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;   // 112 по центру
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)a7_rgb32,
                                  (uint32_t)EMU_W, (uint32_t)EMU_H, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_a7_hw_scaler_active = 1;
            printf("A7800: DE2 GSU scaler %dx%d->%dx%d\n", EMU_W, EMU_H, dw, dh);
        } else {
            printf("A7800: GSU scaler failed, fallback CPU emu_scale\n");
        }
    }
    if (g_a7_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно 320×240 (строки по 320 слов):
        // DE2 PITCH = INSIZE (1280 Б), читает ровно 320 слов на строку.
        // Источник — EMU_FB со страйдом 320 (maria_LineReady уже рендерил).
        const uint16_t* s = EMU_FB;
        uint32_t* d = a7_rgb32;
        for (int y = 0; y < EMU_H; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * EMU_W;
            for (int x = 0; x < EMU_W; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)a7_rgb32,
                        (uint32_t)((size_t)EMU_W * EMU_H * 4u));
    }
}

// r1523: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
extern "C" int a7800_hw_scaler_active(void) { return g_a7_hw_scaler_active; }

// r1523: сброс флага аппаратного скейлера при входе (урок r1496).
extern "C" void a7800_reset_hw_scaler(void) { g_a7_hw_scaler_active = 0; }

// Writable RAM 7800 (16K: 0x0000..0x3FFF). В v8 выделялся из общего пула
// (SCREEN/ChrBuf/RAM/SRAM), но при переходе на FCEUmm те буферы исчезли,
// а a7800_set_memory() не вызывался — memory_ram оставался NULL, и
// memory_Reset() писал по нулевому указателю (чёрный экран/краш).
// Теперь выделяем свой статический буфер.
static uint8_t a7_ram[0x4000];

// r1184: развертка из меню настроек. Вызывается до a7800_init_game: он делает
// prosystem_Reset → region_Reset, которая читает region_type.
//   0 = NTSC, 1 = PAL, 2 = Auto (по байту региона в заголовке картриджа).
extern "C" void a7800_set_tv_mode(int mode) {
    region_type = (mode == 2) ? REGION_AUTO : (mode == 1 ? REGION_PAL : REGION_NTSC);
}

// Эффективный режим после region_Reset(): PAL, если задан явно, либо Auto и
// картридж помечен как PAL (та же логика, что в Region.cpp:182).
extern "C" int a7800_region_pal(void) {
    return (region_type == REGION_PAL ||
            (region_type == REGION_AUTO && cartridge_region == REGION_PAL)) ? 1 : 0;
}

extern byte palette_data[768];

static void a7_build_palette(void) {
    for (int i = 0; i < 256; i++)
        a7_pal_rgb565[i] = (palette_data[i * 3] >> 3 << 11) |
                           (palette_data[i * 3 + 1] >> 2 << 5) |
                           (palette_data[i * 3 + 2] >> 3);
}

void maria_LineReady(const byte* line, int length) {
    if (length > 320) length = 320;
    int top = (int)maria_visibleArea.top;
    int h   = (int)maria_visibleArea.bottom - top + 1;
    int sy  = (int)maria_scanline - top;
    if (sy < 0 || h <= 0) return;
    int y0 = (sy * EMU_H) / h;
    int y1 = ((sy + 1) * EMU_H) / h;
    if (y0 >= EMU_H) return;
    if (y1 > EMU_H) y1 = EMU_H;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < length; x++)
            EMU_FB[y * EMU_W + x] = a7_pal_rgb565[line[x] & 0xFF];
}

static void a7_build_input(byte* input, uint16_t pad) {
    input[0] = (pad & 0x80) ? 0 : 1;
    input[1] = (pad & 0x40) ? 0 : 1;
    input[2] = (pad & 0x20) ? 0 : 1;
    input[3] = (pad & 0x10) ? 0 : 1;
    input[4] = (pad & 0x01) ? 0 : 1;
    input[5] = (pad & 0x02) ? 0 : 1;
    for (int i = 6; i < 12; i++) input[i] = 0;
    // r174: Start -> Reset (запуск), Mode -> Select, Z -> Pause (корпусная)
    input[12] = (pad & 0x08) ? 0 : 1;       // Sega Start -> Reset
    input[13] = (pad & 0x04) ? 0 : 1;       // Mode -> Select
    input[14] = (pad & 0x100) ? 0 : 1;      // Z -> Pause
    input[15] = 0;
    input[16] = 0;
}

static uint16_t pad_from_kbd(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t pad = 0xFF;
    // Бит 0x100 — виртуальная Pause (корпусная кнопка A7800), отдаётся ядру
    // отдельно через prosystem_Pause() в a7800_run_frame.

    // Sega-геймпад: крестовина + A/B + Start + Mode->Select/Pause
    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) pad &= ~0x10;   // Up
    if (sp & 0x0002) pad &= ~0x20;   // Down
    if (sp & 0x0004) pad &= ~0x40;   // Left
    if (sp & 0x0008) pad &= ~0x80;   // Right
    if (sp & 0x0010) pad &= ~0x02;   // A
    if (sp & 0x0020) pad &= ~0x01;   // B
    if (sp & 0x0080) pad &= ~0x08;   // Start
    if (sp & 0x0800) pad &= ~0x04;   // Mode -> Select
    if (sp & 0x0400) pad &= ~0x100;  // Z -> Pause (корпусная)
    // Клавиатура -> A7800 (ремап через Settings → Keyboard remap)
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_UP, keys, n))    pad &= ~0x10;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_DOWN, keys, n))  pad &= ~0x20;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_LEFT, keys, n))  pad &= ~0x40;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_RIGHT, keys, n)) pad &= ~0x80;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_A, keys, n))     pad &= ~0x02;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_B, keys, n))     pad &= ~0x01;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_SELECT, keys, n)) pad &= ~0x04;
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_START, keys, n)) pad &= ~0x08;
    // r0.417 (L12): Pause с клавиатуры (дефолт P), как у Sega-пада (Z)
    if (remap_kbd_pressed(REMAP_PLAT_A7800, BTN_PAUSE, keys, n)) pad &= ~0x100;
    return pad;
}

extern "C" int a7800_init_game(const uint8_t* rom, uint32_t size) {
    // r0.417 (L12 аудита): битый элемент — не запускать (как остальные host-слои)
    if (!rom || size == 0) { printf("[a7800] no ROM\n"); return 0; }
    if (size > sizeof(a7_rom_data)) size = sizeof(a7_rom_data);
    memcpy(a7_rom_data, rom, size);
    a7_rom_size = size;

    // Ключевой момент: без a7800_set_memory память ядра NULL → краш.
    memset(a7_ram, 0, sizeof(a7_ram));
    a7800_set_memory((byte*)a7_ram);

    a7_build_palette();

    if (!cartridge_Load(a7_rom_data, size)) {
        printf("[a7800] cartridge load failed\n");
        return 0;
    }
    prosystem_Reset();
    a7_build_palette();
    // r603: звук A7800 — TIA (всегда) + POKEY (если чип в картридже),
    // буферы byte моно 524/кадр (31440 Гц). DC 120 Гц (непрерывный поток).
    i2s_dc_shift_set(6);   // r943: дефолт 10 (~7.5 Гц) — без среза нижней середины
    printf("[a7800] loaded size=%d pokey=%d\n", (int)size, cartridge_pokey ? 1 : 0);
    return 1;
}

extern "C" void a7800_run_frame(void) {
    static byte input[17];
    uint16_t pad = pad_from_kbd();

    // Корпусная Pause (Z на Sega-геймпаде) — фронт: ядро жмёт паузу
    // один раз (не каждый кадр, иначе она не переключится обратно).
    static int prev_pause = 0;
    int pause_now = !(pad & 0x100);
    if (pause_now && !prev_pause) {
        extern void prosystem_Pause(bool);
        extern bool prosystem_paused;
        prosystem_Pause(!prosystem_paused);
    }
    prev_pause = pause_now;

    a7_build_input(input, pad);   // r174: uint16 — Z (Pause) не режется кастом
    // RAW-читы: пишем байт каждый кадр в RAM 7800 (адреса < 0x4000)
    extern byte *memory_ram;
    int rc = cheats_raw_count();
    for (int i = 0; i < rc; i++) {
        uint32_t a; uint8_t v, c; int hc;
        if (cheats_raw_get(i, &a, &v, &c, &hc)) {
            a &= 0x3FFF;
            if (memory_ram) {
                if (!hc || memory_ram[a] == c) memory_ram[a] = v;
            }
        }
    }
    prosystem_ExecuteFrame(input);

    // Звук A7800 на I2S. TIA и POKEY пишут byte-буферы моно
    // (524 сэмпла/кадр = 31440 Гц при 60fps), тишина буфера = 0, сигнал
    // положительный (TIA 0..30, POKEY 0..120). Смешиваем (TIA+POKEY, если
    // чип есть), ресемпл 31440→48000 (линейный, фаза 16.16), u8→s16.
    //
    // Шаг фазы: (31440<<16)/48000 = 42926. Дробная часть фазы сохраняется
    // между кадрами (интерполяция с накоплением, как в PCE).
    extern byte tia_buffer[];
    extern uint tia_size;
    extern byte pokey_buffer[];
    static uint32_t rs_phase = 0;
    const int in_n = (int)tia_size;            // 524
    int o = 0;
    static int16_t out[880];                   // 524*48000/31440 ≈ 800
    while (o < 880) {
        uint32_t i = rs_phase >> 16;
        if (i + 1 >= (uint32_t)in_n) break;
        uint32_t f = rs_phase & 0xFFFFu;
        int32_t m0 = (int32_t)tia_buffer[i] + (cartridge_pokey ? (int32_t)pokey_buffer[i] : 0);
        int32_t m1 = (int32_t)tia_buffer[i + 1] + (cartridge_pokey ? (int32_t)pokey_buffer[i + 1] : 0);
        int32_t s = m0 + (int32_t)(((int64_t)(m1 - m0) * (int32_t)f) >> 16);
        s <<= 7;                               // 0..150 → 0..19200 (≈60% шкалы)
        if (s > 32767) s = 32767;
        out[o++] = (int16_t)s;
        rs_phase += 42926u;
    }
    rs_phase &= 0xFFFFu;                       // сохранить дробную часть на след. кадр
    // r1346 (Ш2.2): моно→стерео в отдельный буфер и одна подача блоком
    // (один dsb на кадр вместо ~800). Формат пар L,R идентичен push_sample.
    static int16_t out_st[880 * 2];
    for (int i = 0; i < o; i++) {
        out_st[2 * i] = out[i];
        out_st[2 * i + 1] = out[i];
    }
    if (o > 0) i2s_push_block(out_st, (uint32_t)o);
}