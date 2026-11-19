// system_gpgx_h3.c — host-слой Genesis Plus GX для bare-metal H3.
// Покрывает: Sega Mega Drive (ABC/XYZ 6-кнопочный геймпад), Sega Master
// System, Sega Game Gear и Sega SG-1000 (все — единое ядро gpgx).
// Жизненный цикл: gpgx_init_game -> цикл (gpgx_run_frame) -> gpgx_stop.

#include <stdint.h>
#include <string.h>

#include "shared.h"
#include "system.h"
#include "loadrom.h"
#include "genesis.h"
#include "vdp_ctrl.h"
#include "vdp_render.h"
#include "io_ctrl.h"
#include "mem68k.h"
#include "membnk.h"
#include "memz80.h"
#include "sound.h"
#include "cart_hw/md_cart.h"
#include "cart_hw/sms_cart.h"
#include "input_hw/input.h"

#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "emu.h"
#include "emulog.h"
#include "cheatdb.h"
#include "gp_cheats.h"
#include "i2s.h"
#include "fat.h"

extern int printf(const char* fmt, ...);
extern void gb_heap_reset(void);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- глобал ядра, который в оригинале определяет libretro.c ----
t_config config;

// ---- состояние ----
static int g_loaded = 0;
static int g_is_md  = 0;
static int g_force_sms = 0;
static int g_force_sg  = 0;   // r1190: SG-1000 — форс SYSTEM_SG (TMS99xx)
static int g_vp_w = 320;   // последний viewport width
static int g_vp_h = 224;   // последний viewport height

// r1503: DE2 корректно читает 32-битные кадры (PXT), а 16-битные (RGB565)
// — шахматит (проверено на 32X/GBA/Lynx/SNES/GB/NGP: битый DE2-16бит,
// CPU-копия чистая). Game Gear (GPGX) рендерит 160x144 RGB565 в EMU_FB
// (viewport GG, strided 320); в gg_render_frame кадр конвертируется в
// XRGB8888-буфер и отдаётся DE2 GSU, растягивающему на всю высоту FB_H=600
// с сохранением пропорций (dst_w = 160×600/144 = 666, dx=179 по центру —
// как NGP r1501 / GBA r1502). Буфер упакован 160×144 (строки по 160 слов) —
// DE2 PITCH = INSIZE (урок r1495).
static uint32_t gg_rgb32[160 * 144] __attribute__((aligned(4), section(".noinit_fbn")));
// r1503: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale_int).
// file-static — переживает сессии, сбрасывается в gg_init_game (урок r1496).
static int g_gg_hw_scaler_active = 0;

// r1505: 32-битный DE2-буфер кадра Mega Drive (аналог gg_rgb32 r1503, но
// размер viewport переменный: 256/320 (H32/H40) x 192/224/240 — как SNES
// snes_rgb32 r1498). Буфер на максимальный размер viewport (H40/PAL 320×240),
// в md_render_frame пишется только активная область w×h, упакованная
// построчно (строки по w слов, DE2 PITCH = INSIZE — урок r1495).
static uint32_t md_rgb32[320 * 240] __attribute__((aligned(4), section(".noinit_fbn")));
// r1505: активен ли аппаратный DE2 GSU (для emu_run_megadrive — пропустить
// emu_scale). file-static — переживает сессии, сбрасывается при входе в
// emu_run_megadrive (урок r1496: без сброса повторный запуск = чёрный экран).
static int g_md_hw_scaler_active = 0;
// r1506: размер источника, на который слой DE2 откалиброван
// (g_md_gsu_w/g_md_gsu_h). MD-игры меняют режим в рантайме (H32⇄H40,
// NTSC⇄PAL): при смене w/h DE2 читал бы md_rgb32 со старым PITCH
// (урок r1495), строки «разъезжались» (стенд r1505). Перекалибровка —
// только при реальном изменении размера.
static int g_md_gsu_w = 0;
static int g_md_gsu_h = 0;

// Глобальный буфер кадра (как в libretro.c: bitmap_data_[720*576]).
// 64-байтное выравнивание: построчный копипаст viewport в gpgx_render_emu
// идёт без двойных cache-line (псевдонимов) на Cortex-A7.
static uint16_t bitmap_data_[720 * 576] __attribute__((aligned(64)));

// ---- ввод: USB-клавиатура -> геймпад GPGX ----
// Маппинг 6-кнопочного геймпада Mega Drive:
//   Z=A  X=B  C=C  A=X  S=Y  D=Z  Q=Mode  Enter=Start, стрелки=D-Pad
// Для SMS/GG: кнопки 1/2 — Sega A/B (r158), Start = Pause (NMI на SMS),
//   Enter=Start (S replacable), S = корпусная Pause.
// Sega-геймпад (PCF8574): маска Sega -> GPGX (биты разные, ремап).
// ВНИМАНИЕ: биты Input отличаются от Sega-маски! См. input_hw/input.h:
//   INPUT_A=0x40 INPUT_B=0x10 INPUT_C=0x20 INPUT_START=0x80 …,
//   а SMS-кнопки — INPUT_BUTTON1=0x10 (=INPUT_B), INPUT_BUTTON2=0x20 (=INPUT_C).
//   Поэтому A/B для SMS/GG мапятся в INPUT_BUTTON1/2 (иначе «A сдвигается в B/C»).
static void gpgx_poll_input(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t pad = 0;

    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) pad |= INPUT_UP;
    if (sp & 0x0002) pad |= INPUT_DOWN;
    if (sp & 0x0004) pad |= INPUT_LEFT;
    if (sp & 0x0008) pad |= INPUT_RIGHT;
    if (sp & 0x0080) pad |= INPUT_START;    // Start (SMS = пауза NMI)
    if (g_is_md) {
        // MD: A/B/C прямые, X/Y/Z — старшие, Mode — кнопка режима
        if (sp & 0x0010) pad |= INPUT_A;
        if (sp & 0x0020) pad |= INPUT_B;
        if (sp & 0x0040) pad |= INPUT_C;
        if (sp & 0x0100) pad |= INPUT_X;
        if (sp & 0x0200) pad |= INPUT_Y;
        if (sp & 0x0400) pad |= INPUT_Z;
        if (sp & 0x0800) pad |= INPUT_MODE;
    } else {
        // SMS/GG: кнопки Button1/Button2 (INPUT_BUTTON1=0x10, INPUT_BUTTON2=0x20).
        // r158: Sega A -> Button 1, Sega B -> Button 2 (исправлен сдвиг A/B/C).
        if (sp & 0x0010) pad |= INPUT_BUTTON1;
        if (sp & 0x0020) pad |= INPUT_BUTTON2;
    }

    // Клавиатура — через переназначаемый ремап (Settings → Keyboard remap).
    // Платформа: MD (REMAP_PLAT_MD) или SMS/GG/SG-1000 (REMAP_PLAT_SMS /
    // REMAP_PLAT_GG). SG-1000 (r1190) идёт по SMS-ветке (2 кнопки).
    int plat = g_is_md ? REMAP_PLAT_MD
              : (system_hw == SYSTEM_GG ? REMAP_PLAT_GG : REMAP_PLAT_SMS);
    if (remap_kbd_pressed(plat, BTN_UP, keys, n))    pad |= INPUT_UP;
    if (remap_kbd_pressed(plat, BTN_DOWN, keys, n))  pad |= INPUT_DOWN;
    if (remap_kbd_pressed(plat, BTN_LEFT, keys, n))  pad |= INPUT_LEFT;
    if (remap_kbd_pressed(plat, BTN_RIGHT, keys, n)) pad |= INPUT_RIGHT;
    if (g_is_md) {
        if (remap_kbd_pressed(plat, BTN_A, keys, n)) pad |= INPUT_A;
        if (remap_kbd_pressed(plat, BTN_B, keys, n)) pad |= INPUT_B;
        if (remap_kbd_pressed(plat, BTN_C, keys, n)) pad |= INPUT_C;
        if (remap_kbd_pressed(plat, BTN_X, keys, n)) pad |= INPUT_X;
        if (remap_kbd_pressed(plat, BTN_Y, keys, n)) pad |= INPUT_Y;
        if (remap_kbd_pressed(plat, BTN_Z, keys, n)) pad |= INPUT_Z;
        if (remap_kbd_pressed(plat, BTN_MODE, keys, n)) pad |= INPUT_MODE;
    } else {
        // SMS/GG: клавиши A и B = кнопки 1 и 2 (дефолт: Z=A, X=B)
        if (remap_kbd_pressed(plat, BTN_A, keys, n)) pad |= INPUT_BUTTON1;
        if (remap_kbd_pressed(plat, BTN_B, keys, n)) pad |= INPUT_BUTTON2;
    }
    // Start — игровой (MD) / пауза (SMS: NMI) / Start (GG)
    if (remap_kbd_pressed(plat, BTN_START, keys, n)) pad |= INPUT_START;
    // Корпусная Pause (SMS) — отдельная кнопка, в ядре тоже INPUT_START (NMI)
    if (!g_is_md && remap_kbd_pressed(plat, BTN_PAUSE, keys, n)) pad |= INPUT_START;
    input.pad[0] = pad;
    input.pad[1] = 0;
    // ВАЖНО: input.system[] не трогаем — его выставляет input_init (SYSTEM_GAMEPAD)
}

// ---- рендер кадра из bitmap (RGB565) в EMU_FB ----
// vp_w/vp_h — реальный размер viewport ядра на этом кадре:
//   Mode 5: 224/240 строк (NTSC 224, PAL 240), ширина 256/320 (H32/H40)
//   Mode 4 (SMS-совместимость): 192
// Ядро уже сдвинуло строку: в bitmap.data на позиции (line * pitch) лежит
// полная строка шириной vp_w + 2*vp_x, где активная область — vp_w байт,
// начиная с vp_x.
static void gpgx_render_emu(int max_w, int max_h) {
    uint16_t* src = (uint16_t*)bitmap.data;
    if (!src) return;

    int vp_x = bitmap.viewport.x;
    int vp_y = bitmap.viewport.y;
    int vp_w = bitmap.viewport.w;
    int vp_h = bitmap.viewport.h;
    if (vp_w <= 0) vp_w = 256;
    if (vp_h <= 0) vp_h = 192;

    int sw = vp_w > max_w ? max_w : vp_w;
    int sh = vp_h > max_h ? max_h : vp_h;

    g_vp_w = vp_w;
    g_vp_h = vp_h;

    for (int y = 0; y < sh; y++) {
        int sy = (vp_y < 0 ? 0 : vp_y) + y;
        if (sy < 0 || sy >= bitmap.height) continue;
        for (int x = 0; x < sw; x++) {
            int sx = (vp_x < 0 ? 0 : vp_x) + x;
            if (sx < 0 || sx >= bitmap.width) continue;
            EMU_FB[y * EMU_W + x] = src[(size_t)sy * bitmap.width + sx];
        }
    }
}

// r1503: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale_int).
int gg_hw_scaler_active(void) { return g_gg_hw_scaler_active; }

// r1503: рендер кадра Game Gear на DE2 GSU. К этому моменту gg_run_frame уже
// вызвал gpgx_render_emu(160, 144) — картинка 160×144 лежит в EMU_FB (лево-
// верх, страйд 320). Конвертируем её в XRGB8888-буфер, упакованный 160×144
// (строки по 160 слов, DE2 PITCH = INSIZE — урок r1495), и DE2 GSU растягивает
// на всю высоту FB_H=600 с сохранением пропорций (dst_w = 160×600/144 = 666,
// dx по центру — как NGP r1501 / GBA r1502). Fallback — программный
// emu_scale_int из самого EMU_FB (g_gg_hw_scaler_active=0).
void gg_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void h3_de2_set_bkcolor(uint32_t argb);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    if (!g_gg_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (160 * 600) / 144;
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)gg_rgb32,
                                  160, 144, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_gg_hw_scaler_active = 1;
            emulog_field("GG", "scaler", "DE2 GSU 32bpp 160x144->%dx%d", dw, dh);
        } else {
            emulog_field("GG", "scaler", "fallback CPU emu_scale_int");
        }
    }
    if (g_gg_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно 160×144 (строки по 160 слов):
        // DE2 PITCH = INSIZE (640 Б), читает ровно 160 слов на строку.
        // Источник — EMU_FB со страйдом 320 (gpgx_render_emu уже скопировал).
        const uint16_t* s = (const uint16_t*)EMU_FB;
        uint32_t* d = gg_rgb32;
        for (int y = 0; y < 144; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * 160;
            for (int x = 0; x < 160; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)gg_rgb32,
                        (uint32_t)((size_t)160 * 144 * 4u));
    }
}

// r1505: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
int md_hw_scaler_active(void) { return g_md_hw_scaler_active; }

// r1505: рендер кадра Mega Drive на DE2 GSU. К этому моменту gpgx_run_frame
// уже вызвал gpgx_render_emu(320, 240) — viewport 256/320×192/224/240 лежит
// в EMU_FB (лево-верх, страйд 320). Размер кадра берём в ТОМ ЖЕ порядке, что
// программный emu_scale(g_vp_w, g_vp_h) на строке вызова цикла: w = (vp_w<=0
// ? 320 : vp_w), h = (vp_h<=0 ? 224 : vp_h) — переменный (H32⇄H40, NTSC⇄PAL).
// Конвертируем активную область в XRGB8888 (md_rgb32, упакован w×h —
// PITCH = INSIZE, урок r1495), DE2 GSU растягивает на всю высоту FB_H=600 с
// сохранением пропорций (dst_w = w×600/h, кламп 1024, dx по центру — как
// SNES/GG). r1506: слой DE2 калибруется ПОВТОРНО при смене w/h (r1505:
// «строки разъехались» — игры меняют режим в рантайме, DE2 читал md_rgb32
// со старым PITCH). Fallback — программный emu_scale (g_md_hw_scaler_active=0).
void md_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    int w = g_vp_w > 0 ? g_vp_w : 320;
    int h = g_vp_h > 0 ? g_vp_h : 224;
    if (w <= 0 || w > 320) w = 320;
    if (h <= 0 || h > 240) h = 240;
    // r1506: размер сменился (или слой ещё не настроен) — перекалибровка GSU.
    if (!g_md_hw_scaler_active || w != g_md_gsu_w || h != g_md_gsu_h) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (w * 600) / h;
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)md_rgb32,
                                  (uint32_t)w, (uint32_t)h, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_md_hw_scaler_active = 1;
            g_md_gsu_w = w;
            g_md_gsu_h = h;
            emulog_field("MD", "scaler", "DE2 GSU 32bpp %dx%d->%dx%d", w, h, dw, dh);
        } else {
            emulog_field("MD", "scaler", "fallback CPU emu_scale");
        }
    }
    if (g_md_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно w×h (строки по w слов):
        // DE2 PITCH = INSIZE, читает ровно w слов на строку (урок r1495).
        // Источник — EMU_FB со страйдом 320 (gpgx_render_emu уже скопировал).
        const uint16_t* s = (const uint16_t*)EMU_FB;
        uint32_t* d = md_rgb32;
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
        usb_cache_clean((uint32_t)(uintptr_t)md_rgb32,
                        (uint32_t)((size_t)w * h * 4u));
    }
}

// r1509: 32-битный DE2-буфер кадра SMS (фикс. 256×192, как col_rgb32 r1507).
// В sms_render_frame кадр конвертируется в XRGB8888 (строки по 256 слов,
// DE2 PITCH = INSIZE — урок r1495), GSU растягивает на всю высоту FB_H=600 с
// сохранением пропорций (dst_w = 256×600/192 = 800, dx по центру).
static uint32_t sms_rgb32[256 * 192] __attribute__((aligned(4), section(".noinit_fbn")));
// r1509: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
// file-static — переживает сессии, сбрасывается в sms_init_game (урок r1496).
static int g_sms_hw_scaler_active = 0;

// r1509: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
int sms_hw_scaler_active(void) { return g_sms_hw_scaler_active; }

// r1509: рендер кадра SMS на DE2 GSU. К этому моменту sms_run_frame уже
// вызвал gpgx_render_emu(256, 192) — картинка 256×192 лежит в EMU_FB
// (лево-верх, страйд 320). Конвертируем её в XRGB8888-буфер, упакованный
// 256×192 (строки по 256 слов, DE2 PITCH = INSIZE — урок r1495), DE2 GSU
// растягивает на всю высоту FB_H=600 с сохранением пропорций (dst_w =
// 256×600/192 = 800, dx=112 по центру, dy=0 — как Coleco r1507). Fallback —
// программный emu_scale из самого EMU_FB (g_sms_hw_scaler_active=0).
void sms_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void h3_de2_set_bkcolor(uint32_t argb);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    if (!g_sms_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (256 * 600) / 192;   // = 800
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)sms_rgb32,
                                  256, 192, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_sms_hw_scaler_active = 1;
            emulog_field("SMS", "scaler", "DE2 GSU 32bpp 256x192->%dx%d", dw, dh);
        } else {
            emulog_field("SMS", "scaler", "fallback CPU emu_scale");
        }
    }
    if (g_sms_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно 256×192 (строки по 256 слов):
        // DE2 PITCH = INSIZE (1024 Б), читает ровно 256 слов на строку.
        // Источник — EMU_FB со страйдом 320 (gpgx_render_emu уже скопировал).
        const uint16_t* s = (const uint16_t*)EMU_FB;
        uint32_t* d = sms_rgb32;
        for (int y = 0; y < 192; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * 256;
            for (int x = 0; x < 256; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)sms_rgb32,
                        (uint32_t)((size_t)256 * 192 * 4u));
    }
}

// r1510: 32-битный DE2-буфер кадра SG-1000 (фикс. 256×192, как SMS r1509).
// В sg_render_frame кадр конвертируется в XRGB8888 (строки по 256 слов,
// DE2 PITCH = INSIZE — урок r1495), GSU растягивает на всю высоту FB_H=600 с
// сохранением пропорций (dst_w = 256×600/192 = 800, dx по центру).
static uint32_t sg_rgb32[256 * 192] __attribute__((aligned(4), section(".noinit_fbn")));
// r1510: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
// file-static — переживает сессии, сбрасывается в sg_init_game (урок r1496).
static int g_sg_hw_scaler_active = 0;

// r1510: активен ли аппаратный DE2 GSU (для emu.c — пропустить emu_scale).
int sg_hw_scaler_active(void) { return g_sg_hw_scaler_active; }

// r1510: рендер кадра SG-1000 на DE2 GSU. К этому моменту sg_run_frame уже
// вызвал gpgx_render_emu(256, 192) — картинка 256×192 лежит в EMU_FB
// (лево-верх, страйд 320). Конвертируем её в XRGB8888-буфер, упакованный
// 256×192 (строки по 256 слов, DE2 PITCH = INSIZE — урок r1495), DE2 GSU
// растягивает на всю высоту FB_H=600 с сохранением пропорций (dst_w =
// 256×600/192 = 800, dx=112 по центру, dy=0 — как SMS r1509). Fallback —
// программный emu_scale из самого EMU_FB (g_sg_hw_scaler_active=0).
void sg_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void h3_de2_set_bkcolor(uint32_t argb);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    if (!g_sg_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (256 * 600) / 192;   // = 800
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)sg_rgb32,
                                  256, 192, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_sg_hw_scaler_active = 1;
            emulog_field("SG1000", "scaler", "DE2 GSU 32bpp 256x192->%dx%d", dw, dh);
        } else {
            emulog_field("SG1000", "scaler", "fallback CPU emu_scale");
        }
    }
    if (g_sg_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно 256×192 (строки по 256 слов):
        // DE2 PITCH = INSIZE (1024 Б), читает ровно 256 слов на строку.
        // Источник — EMU_FB со страйдом 320 (gpgx_render_emu уже скопировал).
        const uint16_t* s = (const uint16_t*)EMU_FB;
        uint32_t* d = sg_rgb32;
        for (int y = 0; y < 192; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * 256;
            for (int x = 0; x < 256; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)sg_rgb32,
                        (uint32_t)((size_t)256 * 192 * 4u));
    }
}

// ---- Public API ----
int gpgx_init_game(const uint8_t* rom, uint32_t size) {
    g_loaded = 0;
    gb_heap_reset();
    // r1145: «как у эталона» (Genesis Plus GX-фронтенд НЕ добавляет DC-фильтр) —
    // полное отключение DC-блокера CPU2 на пути MD/SMS. Откат фильтра r1143
    // (shift=7), по решению владельца: звук должен быть без ФВЧ, как выводит
    // эталонный Genesis Plus GX.
    i2s_dc_bypass(1);
    // MD/SMS target кольца — адаптивный (emu_throttle), не хардкодим.

    int force_sms = g_force_sms;
    int force_sg  = g_force_sg;
    g_force_sms = 0;
    g_force_sg  = 0;

    if (!rom || size == 0 || size > MAXROMSIZE) {
        printf("GPGX: bad rom\n");
        return 0;
    }

    // сброс глобального состояния ядра
    memset(&cart, 0, sizeof(cart));

    // ROM не копируем — указываем прямо на ROM_BUF (0x50000000)
    cart.rom = (uint8*)rom;
    cart.romsize = size;

    // читы патчат этот ROM (MD — прямая запись слов)
    gp_cheats_set_rom((uint8_t*)rom, size);

    // определяем тип системы: SMS/GG по сигнатуре "TMR SEGA", иначе MD
    system_hw = SYSTEM_MD;
    g_is_md = 1;
    if (force_sg) {
        // r1190: SG-1000 — принудительно (у картриджей SG нет сигнатуры
        // "TMR SEGA"). Ядро переключит VDP на TMS99xx (vdp_z80_data_w_sg)
        // и порты на z80_sg_port_*; регион/частота — как у SMS.
        system_hw = SYSTEM_SG;
        g_is_md = 0;
    } else if (force_sms) {
        system_hw = SYSTEM_SMS;
        g_is_md = 0;
    } else if (size >= 0x4000 && !memcmp(rom + 0x1ff0, "TMR SEGA", 8)) {
        system_hw = SYSTEM_SMS;
        g_is_md = 0;
    } else if (size >= 0x8000 && !memcmp(rom + 0x3ff0, "TMR SEGA", 8)) {
        system_hw = SYSTEM_SMS;
        g_is_md = 0;
    } else if (size >= 0x10000 && !memcmp(rom + 0x7ff0, "TMR SEGA", 8)) {
        system_hw = SYSTEM_SMS;
        g_is_md = 0;
    }

    // инфо из заголовка + регион
    getrominfo((char*)cart.rom);
    get_region((char*)cart.rom);
    romtype = system_hw;   // критично для input_init (выбор типа геймпада)

    // byte-swap ROM под 16-битный доступ (LSB_FIRST) — только для MD.
    // Выполняется прямо в ROM_BUF.
    if (system_hw == SYSTEM_MD || (system_hw & SYSTEM_PBC) == SYSTEM_MD) {
        for (uint32_t i = 0; i + 1 < cart.romsize; i += 2) {
            uint8_t t = cart.rom[i];
            cart.rom[i] = cart.rom[i+1];
            cart.rom[i+1] = t;
        }
    }

    // конфиг по умолчанию (как в libretro.c): MD/SMS, регион auto, без фильтров
    config.system = 0;         /* AUTO */
    config.region_detect = 0;  /* AUTO */
    config.master_clock = 0;
    config.force_dtack = 0;
    config.addr_error = 1;
    config.bios = 0;
    config.lock_on = 0;
    config.add_on = 0;         /* HW_ADDON_NONE */
    config.overscan = 0;
    config.gg_extra = 0;
    config.left_border = 0;
    config.render = 0;
    config.no_sprite_limit = 0;
    config.hq_fm = 0;
    config.hq_psg = 0;
    config.filter = 0;   // r1141: «вывод как у эмулятора» — без low-pass/EQ ядра (llp/rrp не подмешивают DC)
    config.mono = 0;
    // r595: звуковые поля конфига — по первоисточнику (libretro-gpgx
    // config_default()). Раньше НЕ выставлялись → оставались 0 из статики →
    // psg.chanAmp = preamp(0)*… = 0 (PSG тишина = главный звук SGG),
    // fm_preamp=0 и ym2413=0 (FM выключена) → «SGG тишина», хотя картинка и
    // ввод работали. Это НЕ заглушка слоя вывода, а обнулённый конфиг ядра.
    config.psg_preamp  = 150;   /* r645: эталон libretro-gpgx (было 100) */
    config.fm_preamp   = 100;   /* r645: эталон libretro-gpgx (было 60)  */
    config.cdda_volume = 100;
    config.pcm_volume  = 100;
    config.ym2612      = YM2612_DISCRETE;   /* 0 = discrete (как libretro) */
    config.ym2413      = 2;                 /* AUTO: FM(SMS/GG) по региону/картриджу */
    config.hq_fm       = 1;   /* r645: качественный ресемплинг (эталон) */
    config.hq_psg      = 1;   /* r645: качественный ресемплинг (эталон) */

    // ---- настройка ввода: 6-кнопочный геймпад на порту 0 (как libretro.c) ----
    // Единый SYSTEM_GAMEPAD + padtype включает обработку вводов в ядре
    input.system[0] = SYSTEM_GAMEPAD;
    input.system[1] = SYSTEM_GAMEPAD;
    config.input[0].device = DEVICE_PAD6B;
    config.input[0].port = 0;
    config.input[1].device = DEVICE_PAD6B;
    config.input[1].port = 1;
    config.input[0].padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
    config.input[1].padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;

    // ---- инициализация bitmap (как libretro.c:720x576) ----
    // Обязательно до system_init/render_init — ядро рисует в bitmap.data
    bitmap.width  = 720;
    bitmap.height = 576;
    bitmap.pitch  = 720 * 2;
    bitmap.data   = (uint8_t *)bitmap_data_;
    bitmap.viewport.changed = 11;

    // инициализация аппаратуры
    system_init();
    system_reset();

    // Звук: PSG+FM синтез ядра, вывод — gpgx_audio_out() → i2s_push_block().
    // r1346 (Ш1.1): framerate=0 — ядро (core/system.c: audio_set_rate) берёт
    // mclk от system_clock (реальная частота региона: 59.92 NTSC / 49.70 PAL).
    // Прежнее 60.0 жёстко фиксировало темп: PAL получал 800 пар/кадр вместо
    // ~960, FM/PSG-ресемплер настраивался на неверный mclk → «ужасный» звук SEGA.
    // r1351: без проверки результата при отказе blip_new (NULL) первый же
    // blip_end_frame падал по NULL — теперь честный отказ вместо краха.
    if (audio_init(48000, 0) != 0) {
        printf("GPGX: audio init failed (blip alloc)\n");
        return 0;
    }

    g_loaded = 1;
    // применение читов, отмеченных в меню (загружены в rom_browser через cheats_load)
    gp_cheats_compile(g_is_md);
    gp_cheats_apply();

    return 1;   // r0.203: без этого функция «проваливалась» в конец и возвращала
                // мусор → sms_init_game()/emu_run_megadrive() видели !=1 и выходили
                // в меню (GCC -Wreturn-type: system_gpgx_h3.c:263)
}

// ---- вывод звука: PSG+FM (стерео 48к через blip) → I2S ----
// r594: gpgx_audio_out() больше не дренит сэмплы в никуда, а пушит их в
// кольцо I2S (CPU2 выводит). Формат audio_update: стерео пары int16
// (abuf[2i]=L, abuf[2i+1]=R), частота snd.sample_rate = 48000.
// Один патч покрывает SMS/MD/GG (общий host-слой GPGX).
static void gpgx_audio_out(void) {
    static int16_t abuf[4096];
    int n = audio_update(abuf);
    // r1349: защитный кламп (S6 аудита). abuf = 2048 стерео-пар; штатно за кадр
    // набирается ~800..950 пар (48000/59.92 NTSC … 48000/49.70 PAL), но blip
    // (blip_new(4800)) не знает про лимит буфера хоста — при гипотетическом
    // перекосе сэмплов/кадр blip_read_samples записала бы за край abuf.
    if (n > 2048) n = 2048;
    // r1139: пакетная подача одним push_block вместо цикла push_sample.
    // push_sample делает dsb (системный барьер) на КАЖДУЮ пару (~800/кадр на MD);
    // push_block — 1 ожидание места + 1 dsb на блок ~800 пар. Backpressure внутри
    // (ждёт до 75% кольца, дропает переполнение) — при стабильном 48 кГц потока
    // не накапливается, тишина/пауза не «рычит».
    if (n > 0)
        i2s_push_block((const int16_t*)abuf, (uint32_t)n);
}

void gpgx_run_frame(void) {
    if (!g_loaded) return;

    gpgx_poll_input();

    // RAM-читы применяем раз в кадр (игра может перезаписывать память)
    RAMCheatUpdate();

    // генерируем кадр (do_skip=0)
    if (g_is_md)
        system_frame_gen(0);
    else
        system_frame_sms(0);

    // звук
    gpgx_audio_out();

    // рендер в EMU_FB (max_h=240: PAL Mode 5 == 240 строк)
    if (g_is_md)
        gpgx_render_emu(320, 240);
    else
        gpgx_render_emu(256, 192);
}

void gpgx_stop(void) {
    gp_cheats_clear();
    // r1351: снимаем blip-буферы ядра ДО следующего gb_heap_reset(). Раньше
    // указатели snd.blips[] переживали выход, пул перезапускался, и
    // audio_init() следующей сессии делал blip_delete (memset/free) по
    // переиспользованной памяти — порча чужих данных при смене системы.
    extern void audio_shutdown(void);
    audio_shutdown();
    g_loaded = 0;
}

// r1180: регион для раннера SMS (emu.c) — vdp_pal выставляется gpgx по
// заголовку картриджа (core/loadrom.c:1161), снаружи поля не видно.
int gpgx_pal(void) { return vdp_pal ? 1 : 0; }

// r1410: эффективный viewport ядра на последнем кадре (GG: 160x144 при
// gg_extra=0). Берётся до emu_scale-обрезки — то, что игра реально рисует
// внутри растра.
int gpgx_viewport_w(void) { return g_vp_w; }
int gpgx_viewport_h(void) { return g_vp_h; }

// r1350: точный период кадра для throttle. С r1346 звук синтезируется по
// реальному региональному mclk (audio_init(48000, 0) → system_clock), поэтому
// throttle ОБЯЗАН идти с той же частотой кадра. Ровные 16667/20000 мкс
// (60.00/50.00 Гц) не совпадают с реальными 59.9229/49.7015 Гц:
//   NTSC: ядро даёт 801.0 пары/кадр, throttle забирает 800.0 → +1 пара/кадр
//         копится в кольце I2S (~60 пар/с) → через минуту задержка/эхо, «тянется»;
//   PAL:  ядро 965.8 пары/кадр против 960.0 → +6 пар/кадр → порог за ~11 с.
// Период = M-циклы кадра / system_clock = MCYCLES_PER_LINE * строк / clock.
uint32_t gpgx_frame_us(void) {
    uint32_t lines = vdp_pal ? 313u : 262u;
    return (uint32_t)((1000000ull * MCYCLES_PER_LINE * lines) / system_clock);
}

// r1350: тот же период в 16.16 мкс — дробная часть (16688.44 NTSC,
// 20120.13 PAL) компенсируется накопителем в emu_throttle (emu_set_period_us_ex).
uint32_t gpgx_frame_us_q16(void) {
    uint32_t lines = vdp_pal ? 313u : 262u;
    uint64_t num = (uint64_t)1000000u * MCYCLES_PER_LINE * lines;
    return (uint32_t)((num << 16) / system_clock);
}

// ---- точка входа emu.c для Sega Mega Drive ----
void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    // r1505: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_md_hw_scaler_active = 0;
    emulog_begin("MD");
    emulog_field("MD", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    snd_manifest("megadrive", "ym2612 sn76496");
    if (gpgx_init_game(rom, size) != 1) {
        printf("GPGX: init failed\n");
        emulog_field("MD", "init", "FAILED");
        emulog_end("MD");
        return;
    }
    emulog_field("MD", "video", "%dx%d", g_vp_w > 0 ? g_vp_w : 320, g_vp_h > 0 ? g_vp_h : 224);
    emulog_field("MD", "machine", vdp_pal ? "PAL" : "NTSC");
    emulog_field("MD", "init", "OK");
    emu_set_border_color(0x000B1618);   // темно-синий (Mega Drive)
    // r1179: регион — из заголовка ROM. gpgx сам выставляет vdp_pal
    // (core/loadrom.c:1161, бит PAL в регионе), но throttle раньше всегда
    // держал 60 Гц, и PAL-версии шли ~на 20% быстрее и с уплывающим звуком.
    extern uint32_t gpgx_frame_us_q16(void);
    emu_set_period_us_ex(gpgx_frame_us_q16());
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gpgx_run_frame();
        md_render_frame();
        emu_throttle();
        // r1505: при активном DE2 GSU кадр уже на экране (md_render_frame
        // конвертирует RGB565→XRGB8888, GSU растягивает на 600) — программный
        // emu_scale не нужен и наложился бы поверх аппаратного.
        if (!md_hw_scaler_active())
            // размер из viewport ядра: 256/320 (H32/H40) x 192/224/240
            emu_scale(g_vp_w > 0 ? g_vp_w : 320, g_vp_h > 0 ? g_vp_h : 224);
        fb_flush();
        if (emu_esc_hold()) goto exit;   // ESC удержание — выход
    }
exit:
    // r1505: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр MD и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    gpgx_stop();
    emu_audio_stop();   // r939: НЕ вызывался — кольцо не сбрасывалось после
                        // выхода (доезды/«звук ниоткуда»).
    fb_clear(); fb_flush();
    emulog_end("MD");
}

// ---- интерфейс для emu.c: SMS через GPGX (замена smsplus) ----
int sms_init_game(const uint8_t* rom, uint32_t size) {
    // r1509: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_sms_hw_scaler_active = 0;
    g_force_sms = 1;
    return gpgx_init_game(rom, size);
}

void sms_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    RAMCheatUpdate();
    system_frame_sms(0);
    gpgx_audio_out();
    gpgx_render_emu(256, 192);
}

// ---- интерфейс для emu.c: Sega Game Gear через GPGX ----
int gg_init_game(const uint8_t* rom, uint32_t size) {
    g_loaded = 0;
    gb_heap_reset();
    // r1503: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_gg_hw_scaler_active = 0;
    // r1145-продолжение: Game Gear — тот же путь синтеза PSG/FM, что MD/SMS.
    // Единый «вывод как у эталона» без DC-фильтра CPU2 (как gpgx_init_game).
    i2s_dc_bypass(1);
    i2s_dc_shift_set(6);   // r942: дефолт 10 (~7.5 Гц) — без среза нижней середины (r941-регрессия)

    if (!rom || size == 0 || size > MAXROMSIZE) {
        printf("GPGX: bad rom\n"); return 0;
    }

    memset(&cart, 0, sizeof(cart));
    cart.rom = (uint8*)rom;
    cart.romsize = size;

    // читы патчат этот ROM
    gp_cheats_set_rom((uint8_t*)rom, size);

    // форсируем System_GG — ядро сделает VDP GG + viewport 160x144 + рамку
    system_hw = SYSTEM_GG;
    g_is_md = 0;

    getrominfo((char*)cart.rom);
    get_region((char*)cart.rom);
    romtype = SYSTEM_GG;

    config.system = 0;
    config.region_detect = 0;
    config.master_clock = 0;
    config.force_dtack = 0;
    config.addr_error = 1;
    config.bios = 0;
    config.lock_on = 0;
    config.add_on = 0;
    // GG: gg_extra=0 (настоящий 160x144) + overscan=0 — тогда viewport.y=-24
    // и игра ложится ровно в bitmap.data[0..143], рендер (160,144) совпадает
    config.overscan = 0;
    config.gg_extra = 0;
    config.left_border = 0;
    config.render = 0;
    config.no_sprite_limit = 0;
    config.filter = 0;   // r1141: «вывод как у эмулятора» — без low-pass/EQ ядра (llp/rrp не подмешивают DC)
    config.mono = 0;
    // r595: звуковые поля конфига (как в gpgx_init_game и первоисточнике
    // libretro-gpgx config_default()) — без них PSG/FM молчат (preamp=0,
    // FM off) → «SGG тишина».
    config.psg_preamp  = 150;   /* r645: эталон libretro-gpgx */
    config.fm_preamp   = 100;   /* r645: эталон libretro-gpgx */
    config.cdda_volume = 100;
    config.pcm_volume  = 100;
    config.ym2612      = YM2612_DISCRETE;
    config.ym2413      = 2;                 /* AUTO */
    config.hq_fm       = 1;   /* r645: качественный ресемплинг */
    config.hq_psg      = 1;   /* r645: качественный ресемплинг */

    input.system[0] = SYSTEM_GAMEPAD;
    input.system[1] = SYSTEM_GAMEPAD;
    config.input[0].device = DEVICE_PAD6B;
    config.input[0].port = 0;
    config.input[1].device = DEVICE_PAD6B;
    config.input[1].port = 1;

    bitmap.width  = 720;
    bitmap.height = 576;
    bitmap.pitch  = 720 * 2;
    bitmap.data   = (uint8_t *)bitmap_data_;
    bitmap.viewport.changed = 11;

    system_init();
    system_reset();

    // Звук: PSG+FM синтез ядра; вывод — gpgx_audio_out() (i2s_push_block).
    // audio_init обязателен: без него blips[0]==NULL и gg_run_frame падает
    // в blip_end_frame.
    // r1351: поэтому результат ПРОВЕРЯЕМ — при отказе аллокации не запускаем
    // игру с NULL-буфером (раньше падало в первом же кадре).
    if (audio_init(48000, 0) != 0) {
        printf("GPGX: audio init failed (blip alloc)\n");
        return 0;
    }

    g_loaded = 1;

    // применение читов, отмеченных в меню (как в gpgx_init_game)
    gp_cheats_compile(g_is_md);
    gp_cheats_apply();

    return 1;
}

void gg_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    RAMCheatUpdate();
    system_frame_sms(0);
    gpgx_audio_out();
    // настоящий GG: viewport 160x144 в режиме gg_extra=0
    gpgx_render_emu(160, 144);
}

// ---- интерфейс для emu.c: Sega SG-1000 через GPGX ----
// r1190: SG-1000 — самодостаточный слой поверх того же ядра gpgx. От SMS
// отличается тем, что у картриджей SG нет сигнатуры "TMR SEGA": ядро
// форсируется в SYSTEM_SG, тогда VDP встаёт в TMS99xx-режим
// (vdp_z80_data_w_sg / vdp_tms_ctrl_w, vdp_ctrl.c:379) и память 00-3F/80-BF —
// как у SG-1000 (1 КБ RAM + слот картриджа). Ввод — 2 кнопки (PAD2B),
// ремап и рендер — как у SMS (REMAP_PLAT_SMS, system_frame_sms, 256x192).
int sg_init_game(const uint8_t* rom, uint32_t size) {
    // r1510: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_sg_hw_scaler_active = 0;
    g_force_sg = 1;
    return gpgx_init_game(rom, size);
}

void sg_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    RAMCheatUpdate();
    system_frame_sms(0);
    gpgx_audio_out();
    gpgx_render_emu(256, 192);
}