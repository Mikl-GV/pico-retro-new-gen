// system_gpgx_h3.c — host-слой Genesis Plus GX для bare-metal H3.
// Подключает Sega Mega Drive (ABC/XYZ 6-кнопочный геймпад) и SMS/GG.
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
#include "fb_text.h"
#include "emu.h"
#include "cheatdb.h"
#include "gp_cheats.h"
#include "fat.h"

extern int printf(const char* fmt, ...);
extern void gb_heap_reset(void);

// имя текущего ROM (для загрузки читов)
static char g_current_rom_name[FAT_NAME_LEN];
static const char* g_cheat_sys_folder = NULL;

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- глобал ядра, который в оригинале определяет libretro.c ----
t_config config;

// ---- состояние ----
static int g_loaded = 0;
static int g_is_md  = 0;
static int g_force_sms = 0;
static int g_vp_w = 320;   // последний viewport width
static int g_vp_h = 224;   // последний viewport height

// Глобальный буфер кадра (как в libretro.c: bitmap_data_[720*576])
static uint16_t bitmap_data_[720 * 576];

// ---- ввод: USB-клавиатура -> геймпад GPGX ----
// Маппинг 6-кнопочного геймпада Mega Drive:
//   Z=A  X=B  C=C  A=X  S=Y  D=Z  Q=Mode  Enter=Start, стрелки=D-Pad
// Для SMS/GG: S=Pause (Pause на корпусе), Enter=Start
static void gpgx_poll_input(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t pad = 0;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) pad |= INPUT_UP;
        if (sc == 81) pad |= INPUT_DOWN;
        if (sc == 80) pad |= INPUT_LEFT;
        if (sc == 79) pad |= INPUT_RIGHT;
        if (sc == 29) pad |= INPUT_A;       // Z = A
        if (sc == 27) pad |= INPUT_B;       // X = B
        if (sc == 6)  pad |= INPUT_C;       // C = C
        if (sc == 4)  pad |= INPUT_X;       // A = X
        if (sc == 7)  pad |= INPUT_Z;       // D = Z
        if (sc == 20) pad |= INPUT_MODE;    // Q = Mode
        if (sc == 40) pad |= INPUT_START;   // Enter = Start
        if (sc == 22) {
            if (g_is_md)
                pad |= INPUT_Y;              // S = Y (MD)
            else
                pad |= INPUT_START;          // S = Pause (SMS/GG)
        }
    }
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

// ---- Public API ----
int gpgx_init_game(const uint8_t* rom, uint32_t size) {
    printf("GPGX: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    gb_heap_reset();

    int force_sms = g_force_sms;
    g_force_sms = 0;

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
    if (force_sms) {
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
    config.filter = 1;
    config.mono = 0;

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

    g_loaded = 1;
    printf("GPGX: hw=%02X romsize=%u md=%d\n", (unsigned)system_hw,
           (unsigned)cart.romsize, g_is_md);

    // применение читов, отмеченных в меню (загружены в rom_browser через cheats_load)
    gp_cheats_compile(g_is_md);
    gp_cheats_apply();

    printf("GPGX: region=%s vdp_pal=%d sram=%d\n",
           rominfo.country, vdp_pal, sram.on);
    printf("GPGX: viewport %dx%d+%d+%d\n",
           bitmap.viewport.w, bitmap.viewport.h,
           bitmap.viewport.x, bitmap.viewport.y);
    return 1;
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

    // рендер в EMU_FB (max_h=240: PAL Mode 5 == 240 строк)
    if (g_is_md)
        gpgx_render_emu(320, 240);
    else
        gpgx_render_emu(256, 192);
}

void gpgx_stop(void) {
    gp_cheats_clear();
    g_loaded = 0;
    printf("GPGX: stopped\n");
}

// ---- точка входа emu.c для Sega Mega Drive ----
void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (gpgx_init_game(rom, size) != 1) {
        printf("MD(GPGX): init failed\n");
        return;
    }
    emu_set_border_color(0x000B1618);   // темно-синий (Mega Drive)
    uint8_t raw_keys[6];
    emu_throttle_reset();
    for (;;) {
        gpgx_run_frame();
        emu_throttle();
        // размер из viewport ядра: 256/320 (H32/H40) x 192/224/240
        emu_scale(g_vp_w > 0 ? g_vp_w : 320, g_vp_h > 0 ? g_vp_h : 224);
        fb_flush();
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) goto exit;   // ESC — выход
    }
exit:
    gpgx_stop();
    fb_clear(); fb_flush();
}

// ---- интерфейс для emu.c: SMS через GPGX (замена smsplus) ----
int sms_init_game(const uint8_t* rom, uint32_t size) {
    g_force_sms = 1;
    return gpgx_init_game(rom, size);
}

void sms_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    system_frame_sms(0);
    gpgx_render_emu(256, 192);
}

// ---- интерфейс для emu.c: Sega Game Gear через GPGX ----
int gg_init_game(const uint8_t* rom, uint32_t size) {
    printf("GG: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    gb_heap_reset();

    if (!rom || size == 0 || size > MAXROMSIZE) {
        printf("GG: bad rom\n"); return 0;
    }

    memset(&cart, 0, sizeof(cart));
    cart.rom = (uint8*)rom;
    cart.romsize = size;

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
    config.hq_fm = 0;
    config.hq_psg = 0;
    config.filter = 1;
    config.mono = 0;

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

    g_loaded = 1;
    printf("GG: hw=%02X size=%u\n", (unsigned)system_hw, (unsigned)cart.romsize);
    printf("GG: viewport %dx%d+%d+%d\n",
           bitmap.viewport.w, bitmap.viewport.h,
           bitmap.viewport.x, bitmap.viewport.y);
    return 1;
}

void gg_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    system_frame_sms(0);
    // настоящий GG: viewport 160x144 в режиме gg_extra=0
    gpgx_render_emu(160, 144);
}