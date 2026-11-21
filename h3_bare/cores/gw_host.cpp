// gw_host.cpp — Game & Watch / Электроника (murmulator SM5xx core) for H3
// =========================================================================
// Ядро: emulator-refs/murmulator_game_n_watch (src/cpus + src/system), GPLv3.
// Порт под pico-retro-new-gen: ROM приходит от rom_browser в памяти (ROM_BUF),
// парсится gw_romloader(rom,size) -> секции .gwm (фон-индексы, сегменты,
// программа, клавиатура, музыка). Ядро рисует кадр 320x240 в 8-битной
// палитровой рамке (windдe gw_system_blit) и пишет пьезо-звук байтами в
// gw_audio_buffer.
//
// Здесь (host): ресемпл 32768->48000 для I2S, палитра фона bgpal RGB888->
// RGB565, каждый байт кадра (index) -> EMU_FB 320x240, затем
// gw_blit_native_to_de2() — целочисленный масштаб x2 (320x240 -> 640x480) по
// центру DE2-буфера 1024x600, без интерполяции (по решению владельца).
//
// Ввод: gw_get_buttons() — битмаска 8 кнопок (LEFT/UP/RIGHT/DOWN/A/B/TIME/GAME)
// + биты 10/11 (time/alarm). Ремап через Settings. Назначение кнопок — по
// согласованию с владельцем (см. задача «Кнопки»).
#include <stdint.h>
#include <string.h>
#include <stdio.h>
extern "C" {
#include "emu.h"
#include "i2s.h"
#include "h3_hs_timer.h"
#include "fb_text.h"
#include "usb_kbd.h"
#include "btn_pad.h"
#include "remap.h"
// DE2 GSU (аппаратный скалер) и очистка D-cache под DMA — C-linkage.
int  h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                           uint32_t bpp, uint32_t dx, uint32_t dy,
                           uint32_t dw, uint32_t dh);
void h3_de2_ui_restore(void);
void usb_cache_clean(uint32_t addr, uint32_t size);
}

#include "gw/system/gw_system.h"
#include "gw/system/gw_romloader.h"

#define EMU_FB   ((uint16_t*)0x5F800000)
#define EMU_W    320
#define EMU_H    240

// DE2-буфер HDMI (XRGB8888 1024x600) — для нативного блита родного размера GW.
#define FB_W_NATIVE 1024
#define FB_H_NATIVE 600

// ---- основные константы ядра ----
// Системная частота = 32768 Гц (GW_SYS_FREQ). Видеоядро рефрешится 480 раз/с
// (GW_REFRESH_RATE): кадр 60 Гц = 32768/480 = 68 циклов x 8 субкадров.
#define GW_CYCLES_PER_SUBFRAME (GW_SYS_FREQ / GW_REFRESH_RATE)  // 68
#define GW_SUBFRAMES_PER_FRAME (GW_REFRESH_RATE / 60)            // 8

// ---- состояние игры ----
static int g_loaded = 0;

// Палитра фона: из bgpal (RGB888 triplets), по правилам updatePaletteGame:
// индекс rgb = pal[i] с четырьмя сдвигами яркости (j*PALETTE_COL_DEC, j=1:-12)
// на индексах i + PALETTE_IDX_INC*j. Плюс index 0 = чёрный.
#define PALETTE_IDX_INC 50
#define PALETTE_COL_DEC 48
static uint16_t gw_pal_rgb565[256];

static void gw_build_palette(void) {
    memset(gw_pal_rgb565, 0, sizeof(gw_pal_rgb565));
    if (!gw_background_pal) return;
    uint32_t len = gw_head.background_palette_size;   // байт
    uint32_t n = len / 3;
    if (n > 200) n = 200;
    const uint8_t* p = (const uint8_t*)gw_background_pal;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < 4; j++) {
            int R = p[i * 3 + 0];
            int G = p[i * 3 + 1];
            int B = p[i * 3 + 2];
            if (j == 1) { R -= 12; G -= 12; B -= 12; }
            else        { R -= PALETTE_COL_DEC * (int)j;
                          G -= PALETTE_COL_DEC * (int)j;
                          B -= PALETTE_COL_DEC * (int)j; }
            if (R < 0) R = 0;
            if (G < 0) G = 0;
            if (B < 0) B = 0;
            if (R > 255) R = 255;
            if (G > 255) G = 255;
            if (B > 255) B = 255;
            uint32_t idx = i + PALETTE_IDX_INC * j;
            if (idx < 256)
                gw_pal_rgb565[idx] = (uint16_t)(((R & 0xF8) << 8) |
                                                ((G & 0xFC) << 3) |
                                                (B >> 3));
        }
    }
    // фон-байты обычно 0..n-1 (индексы), старшие индексы (сегменты) остаются
    // тёмными/чёрными — на них ляжет mix фона по правилам rgb_multiply_8bits.
}

// ---- рендер кадра ядра -> EMU_FB (индексы -> RGB565) ----
static uint8_t gw_frame_indices[EMU_W * EMU_H]
    __attribute__((aligned(4), section(".noinit_fbn")));

// Упакованный 32bpp-кадр для аппаратного скалера DE2 (16bpp DE2 на этом
// железе читает с артефактами — проверено на 32X/GBA/Lynx/SNES/GB, r1495).
static uint32_t gw_rgb32[EMU_W * EMU_H]
    __attribute__((aligned(4), section(".noinit_fbn")));
static int g_gw_hw_scaler = 0;   // 1 = DE2 GSU активен (CPU-блит не нужен)

extern "C" void gw_render_frame(void) {
    if (!g_loaded) return;

    // r1544: кадр-буфер каждый раз предзаполняем ФОНОМ, как эталон gw_set_background().
    // Рендер ядра (gw_system_blit) пишет ТОЛЬКО сегменты поверх mixer (фона) и НЕ
    // заполняет прозрачные пиксели. Без этого фон чёрный (gw_frame_indices в
    // .noinit_fbn, не инициализирован), а при перезаходе наследуются индексы
    // прошлой игры. Фон: для обычного LCD — копия gw_background, для
    // инвертированного — индекс 0 (чёрный) по FLAG_RENDERING_LCD_INVERTED.
    if (gw_background && gw_head.background_pixel_size >= (uint32_t)(EMU_W * EMU_H)) {
        memcpy(gw_frame_indices, gw_background, (size_t)EMU_W * EMU_H);
    } else if (gw_head.flags & FLAG_RENDERING_LCD_INVERTED) {
        memset(gw_frame_indices, 0, (size_t)EMU_W * EMU_H);
    } else {
        memset(gw_frame_indices, 0, (size_t)EMU_W * EMU_H);
    }
    gw_system_blit(gw_frame_indices);   // ядро пишет 8-бит индексы/байты

    // Каждый байт кадра — индекс в палитре, конвертируем 1:1 в EMU_FB.
    // Кадр 320x240 совпадает с EMU_FB по размеру.
    const uint8_t* s = gw_frame_indices;
    uint16_t* d = EMU_FB;
    for (int i = 0; i < EMU_W * EMU_H; i++) {
        uint8_t idx = s[i];
        d[i] = gw_pal_rgb565[idx];
    }
}

// ---- ввод: битмаска кнопок для ядра ----
// Реализация gw_get_buttons(), которую ожидает ядро (gw_system.c:673 и др).
// Кнопки G&W: LEFT/UP/RIGHT/DOWN/A/B/TIME/GAME -> 0x01..0x80.
// TIME/GAME — служебные (часы/смена игры) — выводятся на Select/Start.
extern "C" uint32_t gw_get_buttons(void);

uint32_t gw_get_buttons(void) {
    uint8_t raw[6];
    int n = usb_kbd_get_raw(raw, 6);
    uint32_t b = 0;

    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) b |= GW_BUTTON_UP;
    if (sp & 0x0002) b |= GW_BUTTON_DOWN;
    if (sp & 0x0004) b |= GW_BUTTON_LEFT;
    if (sp & 0x0008) b |= GW_BUTTON_RIGHT;
    if (sp & 0x0010) b |= GW_BUTTON_A;
    if (sp & 0x0020) b |= GW_BUTTON_B;
    if (sp & 0x0080) b |= GW_BUTTON_GAME;    // Sega Start
    // TIME/ALARM — не задействованы (часы-режим не нужен в эмуляции)

    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_UP,    raw, n)) b |= GW_BUTTON_UP;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_DOWN,  raw, n)) b |= GW_BUTTON_DOWN;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_LEFT,  raw, n)) b |= GW_BUTTON_LEFT;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_RIGHT, raw, n)) b |= GW_BUTTON_RIGHT;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_A,     raw, n)) b |= GW_BUTTON_A;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_B,     raw, n)) b |= GW_BUTTON_B;
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_START, raw, n)) b |= GW_BUTTON_GAME;
    // r1547: TIME (часы/демонстрация) и ALARM (будильник) — программные
    // комбинации ядра: бит 10 = B+TIME (KEY_SOFT_TIME), бит 11 = B+GAME
    // (KEY_SOFT_ALARM), см. gw_system.c:428-443.
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_MODE,   raw, n)) b |= (1u << 10);
    if (remap_kbd_pressed(REMAP_PLAT_GW, BTN_SELECT, raw, n)) b |= (1u << 11);
    return b;
}

// ---- звук: байты gw_audio_buffer (пищалка) -> PCM -> I2S ----
// Кадр 60 Гц = 8 субкадров по 68 циклов (GW_REFRESH_RATE 480). За каждый
// субкадр ядро пишет ровно GW_AUDIO_BUFFER_LENGTH=68 байт пищалки в
// gw_audio_buffer (всегда с индекса 0, см. gw_system_sound_melody). Хосту
// нужно потреблять байты и снимать флаг gw_audio_buffer_copied, иначе буфер
// (68*2) переполнится и сдвиг ядра испортит поток.
// Байты = сэмплы 32768 Гц. За кадр набираем 544 байта (=544 сэмпла 32768,
// длительность = 1/60с), ресемплем в 48000 -> 48000/60 = 800 пар.
#define GW_RS_INC ((32768u << 16) / 48000u)
#define GW_BYTES_PER_FRAME  (GW_SUBFRAMES_PER_FRAME * GW_AUDIO_BUFFER_LENGTH)  // 544
#define GW_OUT_PER_FRAME    800
static int16_t gw_pcm_hot[GW_BYTES_PER_FRAME]
    __attribute__((unused));

static int16_t gw_byte_to_pcm(uint8_t v) {
    // пищалка: бит 0 -> полный размах (умеренно, чтобы не резать)
    return (v & 1) ? 1800 : -1800;
}

// Потребление одного субкадра: 68 байт с index 0, затем copied=true (ядро
// сдвинет буфер при следующем R-write).
extern "C" void gw_audio_subframe(int16_t* dst) {
    for (int i = 0; i < GW_AUDIO_BUFFER_LENGTH; i++)
        dst[i] = gw_byte_to_pcm(gw_audio_buffer[i]);
    gw_audio_buffer_copied = true;
}

extern "C" void gw_audio_frame(const int16_t* pcm, int16_t* out) {
    // Линейный 32768 -> 48000 из pcm[0..543] -> out[0..1599] (800 пар).
    uint32_t ph = 0;   // фаза этой сборки
    uint32_t o = 0;
    while (o < GW_OUT_PER_FRAME) {
        uint32_t i = ph >> 16;
        if (i + 1 >= GW_BYTES_PER_FRAME) break;
        uint32_t f = ph & 0xFFFFu;
        int32_t a = pcm[i], b = pcm[i + 1];
        int16_t s = (int16_t)(a + (((b - a) * (int32_t)f) >> 16));
        out[2 * o] = s; out[2 * o + 1] = s;
        o++;
        ph += GW_RS_INC;
    }
    if (o) i2s_push_block(out, o);
}

extern "C" void gw_system_shutdown(void);   // определён в gw_system.c, нет в .h

// ---- init ----
extern "C" int gw_init_game(const uint8_t* rom, uint32_t size) {
    g_loaded = 0;
    if (!rom || size < ROM_HEADER_SIZE) return 0;
    if (!gw_romloader(rom, size)) return 0;

    gw_build_palette();
    gw_system_sound_init();
    if (!gw_system_config()) { printf("GW: config failed\n"); return 0; }
    // фон в кадр-буфер: blit рисует по индексам фона + сегменты
    gw_system_start();
    gw_system_reset();
    g_loaded = 1;
    printf("GW: loaded cpu='%.5s' sig='%.8s'\n",
           gw_head.cpu_name, gw_head.rom_signature);
    return 1;
}

extern "C" uint32_t gw_frame_period_us(void) {
    return 16667u;   // ~60 Гц для G&W (ядро тактовается 32768 Гц по фикс. кадру)
}

// ---- выполнение кадра ----
extern "C" void gw_run_frame(void) {
    if (!g_loaded) return;

    // r1547: AC-сброс (кнопка RESET, по умолчанию R) — по ФРОНТУ, не удержанием.
    // Ядро не поддерживает pause/power (биты 8/9 игнорируются, gw_system.c:288),
    // поэтому сброс = перезапуск состояния ядра.
    {
        static int prev_reset = 0;
        uint8_t raw[6];
        int n = usb_kbd_get_raw(raw, 6);
        int now = remap_kbd_pressed(REMAP_PLAT_GW, BTN_RESET, raw, n);
        if (now && !prev_reset) gw_system_reset();
        prev_reset = now;
    }

    // Кадр 60 Гц: 8 субкадров по 68 циклов (544 цикла 32768 Гц). После каждого
    // субкадра ядро наполнило gw_audio_buffer[0..67]; собираем PCM и снимаем
    // флаг copied, затем ресемплем всю пачку 544->800 пар в конце кадра.
    static int16_t pcm[GW_BYTES_PER_FRAME];
    static int16_t out[GW_OUT_PER_FRAME * 2];
    uint32_t base = 0;
    for (int s = 0; s < GW_SUBFRAMES_PER_FRAME; s++) {
        gw_system_run(GW_CYCLES_PER_SUBFRAME);
        gw_audio_subframe(&pcm[base]);
        base += GW_AUDIO_BUFFER_LENGTH;
    }
    gw_audio_frame(pcm, out);
}

// Конвертация RGB565 -> XRGB8888 одного пикселя.
static inline uint32_t gw_rgb565_to_xrgb(uint16_t p) {
    uint32_t r = (p >> 11) & 0x1F;
    uint32_t g = (p >> 5)  & 0x3F;
    uint32_t b = p & 0x1F;
    return (r << 19) | (g << 10) | (b << 3);
}

// Вывод кадра G&W на HDMI: целочисленный ×2 (320×240 → 640×480) по центру
// 1024×600. Основной путь — аппаратный скалер DE2 GSU (кадр отдаётся 32bpp
// XRGB8888: DE2-16bpp на этом железе читает с артефактами, r1495).
// Если GSU недоступен — CPU-fallback: блоки 2×2 в DE2-буфер напрямую.
#define GW_SCALE 2

// Автоматический выбор пути: DE2 GSU ×2 либо CPU ×2.
static void gw_try_hw_scaler(void) {
    const uint32_t dw = (uint32_t)EMU_W * GW_SCALE;   // 640
    const uint32_t dh = (uint32_t)EMU_H * GW_SCALE;   // 480
    const uint32_t dx = (FB_W_NATIVE - dw) / 2;       // 192
    const uint32_t dy = (FB_H_NATIVE - dh) / 2;       // 60
    if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)gw_rgb32,
                              EMU_W, EMU_H, 32, dx, dy, dw, dh) == 0)
        g_gw_hw_scaler = 1;
}

// CPU-fallback: целочисленный ×2 блоками 2×2 в DE2-буфер, поля чёрные.
static void gw_blit_cpu_x2(void) {
    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();
    const uint16_t* src = EMU_FB;
    const int dw = EMU_W * GW_SCALE;              // 640
    const int dh = EMU_H * GW_SCALE;              // 480
    const int dx = (FB_W_NATIVE - dw) / 2;        // 192
    const int dy = (FB_H_NATIVE - dh) / 2;        // 60

    // Поля сверху/снизу
    for (int y = 0; y < dy; y++)
        for (int x = 0; x < FB_W_NATIVE; x++) dst[y * FB_W_NATIVE + x] = 0;
    for (int y = dy + dh; y < FB_H_NATIVE; y++)
        for (int x = 0; x < FB_W_NATIVE; x++) dst[y * FB_W_NATIVE + x] = 0;

    for (int sy = 0; sy < EMU_H; sy++) {
        const uint16_t* srow = src + (size_t)sy * EMU_W;
        // одна строка источника -> GW_SCALE строк буфера
        for (int k = 0; k < GW_SCALE; k++) {
            uint32_t* drow = dst + (size_t)(dy + sy * GW_SCALE + k) * FB_W_NATIVE;
            for (int x = 0; x < dx; x++) drow[x] = 0;
            for (int x = dx + dw; x < FB_W_NATIVE; x++) drow[x] = 0;
            uint32_t* p = drow + dx;
            for (int sx = 0; sx < EMU_W; sx++) {
                uint32_t c = gw_rgb565_to_xrgb(srow[sx]);
                for (int j = 0; j < GW_SCALE; j++) *p++ = c;
            }
        }
    }
    fb_dirty_all();
    fb_frame_scaled();
}

extern "C" void gw_blit_native_to_de2(void) {
    if (!g_gw_hw_scaler)
        gw_try_hw_scaler();

    if (g_gw_hw_scaler) {
        // Аппаратный путь: конвертируем кадр в упакованный 32bpp (строки по
        // EMU_W слов — DE2 PITCH = src_w*4) и чистим D-cache под DMA.
        const uint16_t* s = EMU_FB;
        uint32_t* d = gw_rgb32;
        for (int i = 0; i < EMU_W * EMU_H; i++)
            d[i] = gw_rgb565_to_xrgb(s[i]);
        usb_cache_clean((uint32_t)(uintptr_t)gw_rgb32,
                        (uint32_t)((size_t)EMU_W * EMU_H * 4u));
        return;
    }

    gw_blit_cpu_x2();
}

// ---- выход ----
extern "C" void gw_stop(void) {
    if (!g_loaded) { printf("GW: stop (not loaded)\n"); return; }
    g_loaded = 0;
    gw_system_shutdown();
    // Вернуть DE2 штатный UI-слой (1:1 на буфер меню), иначе после выхода
    // меню рисуется в обрезанную/сдвинутую область скалера.
    if (g_gw_hw_scaler) {
        h3_de2_ui_restore();
        g_gw_hw_scaler = 0;
    }
    printf("GW: stopped\n");
}

// ---- точка входа диспетчера ----
extern "C" void emu_run_gw(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    if (gw_init_game(rom, size) != 1) {
        printf("GW: init failed\n");
        return;
    }
    emu_set_period_us(gw_frame_period_us());
    emu_throttle_reset();
    emu_esc_hold_reset();
    emu_set_border_color(0xFF000000);
    for (;;) {
        gw_run_frame();
        emu_throttle();
        gw_render_frame();
        gw_blit_native_to_de2();   // родное 320x240, без масштабирования
        fb_flush();
        if (emu_esc_hold()) break;
    }
    gw_stop();
    emu_audio_stop();
    fb_clear(); fb_flush();
}