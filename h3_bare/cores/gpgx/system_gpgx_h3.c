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

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- глобал ядра, который в оригинале определяет libretro.c ----
t_config config;

// ---- состояние ----
static int g_loaded = 0;
static int g_is_md  = 0;
static int g_force_sms = 0;

// ---- ввод: USB-клавиатура -> геймпад GPGX ----
// Маппинг 6-кнопочного геймпада Mega Drive:
//   Z=A  X=B  C=C  A=X  S=Y  D=Z  Q=Mode  Enter=Start, стрелки=D-Pad
static void gpgx_poll_input(void) {
    memset(&input, 0, sizeof(input));

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
        if (sc == 22) pad |= INPUT_Y;       // S = Y
        if (sc == 7)  pad |= INPUT_Z;       // D = Z
        if (sc == 20) pad |= INPUT_MODE;    // Q = Mode
        if (sc == 40) pad |= INPUT_START;   // Enter = Start
    }
    input.pad[0] = pad;
    input.pad[1] = 0;
    input.system[0] = 0;
    input.system[1] = 0;
}

// ---- рендер кадра из bitmap (RGB565) в EMU_FB ----
static void gpgx_render_emu(int out_w, int out_h) {
    uint16_t* src = (uint16_t*)bitmap.data;
    int sw = bitmap.width;
    int sh = bitmap.height;
    if (sw <= 0 || sh <= 0) return;

    int vx0 = bitmap.viewport.x < 0 ? 0 : bitmap.viewport.x;
    int vy0 = bitmap.viewport.y < 0 ? 0 : bitmap.viewport.y;
    int w = out_w > EMU_W ? EMU_W : out_w;
    int h = out_h > EMU_H ? EMU_H : out_h;

    for (int y = 0; y < h; y++) {
        int sy = vy0 + y;
        if (sy >= sh) break;
        for (int x = 0; x < w; x++) {
            int sx = vx0 + x;
            if (sx >= sw) break;
            EMU_FB[y * EMU_W + x] = src[(size_t)sy * bitmap.width + sx];
        }
    }
}

// ---- Public API ----
int gpgx_init_game(const uint8_t* rom, uint32_t size) {
    printf("GPGX: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    if (!rom || size == 0 || size > MAXROMSIZE) {
        printf("GPGX: bad rom\n");
        return 0;
    }

    // сброс глобального состояния ядра
    memset(&cart, 0, sizeof(cart));

    // копируем ROM в буфер картриджа
    memcpy(cart.rom, rom, size);
    cart.romsize = size;

    // определяем тип системы: SMS/GG по сигнатуре "TMR SEGA", иначе MD
    system_hw = SYSTEM_MD;
    g_is_md = 1;
    if (g_force_sms) {
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

    // byte-swap ROM под 16-битный доступ (LSB_FIRST) — только для MD
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

    // инициализация аппаратуры
    system_init();
    system_reset();

    g_loaded = 1;
    printf("GPGX: hw=%02X romsize=%u md=%d\n", (unsigned)system_hw,
           (unsigned)cart.romsize, g_is_md);
    return 1;
}

void gpgx_run_frame(void) {
    if (!g_loaded) return;

    gpgx_poll_input();

    // генерируем кадр (do_skip=0)
    if (g_is_md)
        system_frame_gen(0);
    else
        system_frame_sms(0);

    // рендер в EMU_FB
    if (g_is_md)
        gpgx_render_emu(320, 224);
    else
        gpgx_render_emu(256, 192);
}

void gpgx_stop(void) {
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
    uint8_t raw_keys[6];
    for (;;) {
        gpgx_run_frame();
        emu_throttle();
        emu_scale(320, 224);
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
    int r = gpgx_init_game(rom, size);
    g_force_sms = 0;
    return r;
}

void sms_run_frame(void) {
    if (!g_loaded) return;
    gpgx_poll_input();
    system_frame_sms(0);
    gpgx_render_emu(256, 192);
}

void sms_render_frame(void) {
    gpgx_render_emu(256, 192);
}