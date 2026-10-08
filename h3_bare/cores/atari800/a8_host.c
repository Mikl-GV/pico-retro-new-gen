// a8_host.c — Atari 8-bit (400/800/XL/XE) host for H3 bare-metal.
//
// Ядро: h3_bare/cores/atari800 (vendored libretro-core atari800 + ядро).
// Мы — тонкий libretro-фронтенд: определяем retro_* колбэки (см. a8_fs.c —
// файловый слой для ROM), вызываем a8_retro_init/load_game/run, кадр RGB565
// (Retro_Screen, RENDER16B) переносим в EMU_FB (320x240) -> emu_scale().
//
// Ввод: USB-клавиатура (HID -> RETROK) + Sega-геймпад/кнопочный пад
// (RETRO_DEVICE_JOYPAD). Карта A800 (как в libretro-core.c, descriptors_a800):
//   Joystick: DPAD + A=Fire1, B=Fire2, Y=Space/Fire3, X=Return,
//             Select=Console Select, Start=Console Start, L=Option, L2=Esc, R2=Help.
//   Клавиатура: RETROK_* (HID->RETROK маппинг, как в fuse/bk).
//
// Звук: НЕ выводится (r777 — звуковой слой удалён). retro_audio_* — no-op.
// ROM: файл (xex/car/bin/atr/cas/... ) грузится rom_browser в ROM_BUF и
// регистрируется в a8_fs_register() под путём, который видит враппер как
// info->path (см. a8_fs.c).
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "libretro.h"

#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "fat.h"
#include "i2s.h"          // no-op стабы (oбязателен: враппер зовёт audio_cb)

// ---- a8_rename.sh переименовывает всё в a8_*; прототипы враппера ----
void a8_retro_set_environment(retro_environment_t);
void a8_retro_set_video_refresh(retro_video_refresh_t);
void a8_retro_set_audio_sample(retro_audio_sample_t);
void a8_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void a8_retro_set_input_poll(retro_input_poll_t);
void a8_retro_set_input_state(retro_input_state_t);
void a8_retro_init(void);
bool a8_retro_load_game(const struct retro_game_info*);
void a8_retro_run(void);
void a8_retro_unload_game(void);
void a8_retro_deinit(void);

// a8_fs.c: регистрация ROM-буфера под путём (для retro_vfs_fopen->fopen)
void a8_fs_register(const char* path, const void* data, uint32_t size);

// Эмуляция Atari 8-bit: карта кнопок — как в ядре (descriptors_a800)
#define EMU_FB_ADDR 0x5F800000u
#define EMU_FB_W    320
#define EMU_FB_H    240

// ---- ROM: путь из rom_browser (имя файла), буфер в ROM_BUF ----
static char     g_path[128] = "game.xex";
static uint16_t g_joy = 0;                    // биты RETRO_DEVICE_ID_JOYPAD

// ---- ввод: HID-сканкод -> RETROK (тот же маппинг, что в fuse/bk) ----
static uint16_t hid_to_retrok(uint8_t sc)
{
    switch (sc) {
    case 0x04: return RETROK_a;   case 0x05: return RETROK_b;   case 0x06: return RETROK_c;
    case 0x07: return RETROK_d;   case 0x08: return RETROK_e;   case 0x09: return RETROK_f;
    case 0x0A: return RETROK_g;   case 0x0B: return RETROK_h;   case 0x0C: return RETROK_i;
    case 0x0D: return RETROK_j;   case 0x0E: return RETROK_k;   case 0x0F: return RETROK_l;
    case 0x10: return RETROK_m;   case 0x11: return RETROK_n;   case 0x12: return RETROK_o;
    case 0x13: return RETROK_p;   case 0x14: return RETROK_q;   case 0x15: return RETROK_r;
    case 0x16: return RETROK_s;   case 0x17: return RETROK_t;   case 0x18: return RETROK_u;
    case 0x19: return RETROK_v;   case 0x1A: return RETROK_w;   case 0x1B: return RETROK_x;
    case 0x1C: return RETROK_y;   case 0x1D: return RETROK_z;
    case 0x1E: return RETROK_1;   case 0x1F: return RETROK_2;   case 0x20: return RETROK_3;
    case 0x21: return RETROK_4;   case 0x22: return RETROK_5;   case 0x23: return RETROK_6;
    case 0x24: return RETROK_7;   case 0x25: return RETROK_8;   case 0x26: return RETROK_9;
    case 0x27: return RETROK_0;
    case 0x28: return RETROK_RETURN;     case 0x29: return RETROK_ESCAPE;
    case 0x2A: return RETROK_BACKSPACE;  case 0x2B: return RETROK_TAB;
    case 0x2C: return RETROK_SPACE;
    case 0x2D: return RETROK_MINUS;      case 0x2E: return RETROK_EQUALS;
    case 0x2F: return RETROK_LEFTBRACKET;case 0x30: return RETROK_RIGHTBRACKET;
    case 0x31: return RETROK_BACKSLASH;  case 0x33: return RETROK_SEMICOLON;
    case 0x34: return RETROK_QUOTE;      case 0x35: return RETROK_BACKQUOTE;
    case 0x36: return RETROK_COMMA;      case 0x37: return RETROK_PERIOD;
    case 0x38: return RETROK_SLASH;      case 0x39: return RETROK_CAPSLOCK;
    case 0x4F: return RETROK_RIGHT;      case 0x50: return RETROK_LEFT;
    case 0x51: return RETROK_DOWN;       case 0x52: return RETROK_UP;
    default: return 0;
    }
}

// ---- ввод: обновить g_kbd (RETROK->нажата) и g_joy (геймпад) ----
static uint8_t g_kbd[RETROK_LAST];

static void host_update_input(void)
{
    memset(g_kbd, 0, sizeof(g_kbd));
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    for (int i = 0; i < n; i++) {
        uint16_t rk = hid_to_retrok(keys[i]);
        if (rk) g_kbd[rk] = 1;
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) g_kbd[RETROK_LSHIFT] = 1;
    if (mods & 0x20) g_kbd[RETROK_RSHIFT] = 1;
    if (mods & 0x01) g_kbd[RETROK_LCTRL] = 1;
    if (mods & 0x10) g_kbd[RETROK_RCTRL] = 1;
    if (mods & 0x04) g_kbd[RETROK_LALT] = 1;
    if (mods & 0x40) g_kbd[RETROK_RALT] = 1;

    // Sega-геймпад + кнопочный пад -> джойстик A800
    uint16_t sp = pad_scan_combined();
    uint16_t j = 0;
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // Fire 1
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);      // Fire 2
    if (sp & 0x0040) j |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);      // Space/Fire 3
    if (sp & 0x0100) j |= (1u << RETRO_DEVICE_ID_JOYPAD_X);      // Return
    if (sp & 0x0800) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT); // Console Select
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);  // Console Start
    if (sp & 0x0200) j |= (1u << RETRO_DEVICE_ID_JOYPAD_L);      // Option
    if (sp & 0x0400) j |= (1u << RETRO_DEVICE_ID_JOYPAD_R2);     // Help
    g_joy = j;
}

static void host_input_poll(void) { host_update_input(); }

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    if (port == 0 && device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return (int16_t)g_joy;
        return (g_joy & (1u << id)) ? 1 : 0;
    }
    return 0;
}

// ---- звук: выключен (r777), no-op ----
static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t host_audio_sample_batch(const int16_t* d, size_t f) { (void)d; return f; }

// ---- кадр RGB565 (Retro_Screen) -> EMU_FB ----
static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    uint16_t* dst = (uint16_t*)EMU_FB_ADDR;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;
    unsigned w = width  < EMU_FB_W ? width  : EMU_FB_W;
    unsigned h = height < EMU_FB_H ? height : EMU_FB_H;
    emu_clear_fb();
    for (unsigned y = 0; y < h; y++)
        memcpy(dst + (size_t)y * EMU_FB_W, src + (size_t)y * sp, w * sizeof(uint16_t));
}

// ---- environment: дефолты. Ключевое: SET_PIXEL_FORMAT(RGB565) = true ----
static void a8_log(enum retro_log_level lvl, const char* fmt, ...) {
    (void)lvl; (void)fmt;
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        // r779: обязателен валидный log_cb — иначе ядро берёт мусор из стека
        // и первый же log_cb() даёт Prefetch Abort (P:00000070:0E).
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = a8_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;  // RETRO_DIR = "." (retro_init)
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        // r779: отдаём ядру «внутреннее разрешение» 320x240 — ядро само
        // центрирует ANTIC-кадр (384x240) в 320x240, host_video копирует 1:1
        // без обрезания, а emu_scale(320,240) растягивает по высоте экрана.
        // Без этого retrow/retroh остаются 400x300 и кадр режется в host_video.
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key && strcmp(v->key, "atari800_resolution") == 0) {
            v->value = "320x240";
            return true;
        }
        return false;   // остальные опции — дефолты ядра
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        return false;  // все опции — дефолты ядра (800XL 64K, NTSC)
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        return false;  // используем базовый disk control (нет дисков)
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        return false;
    default:
        return false;
    }
}

void emu_run_atari800(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    emu_prepare();
    snd_manifest("atari800", "pokey");

    // BASIC-режим: rom==NULL → запускаем ядро без картриджа (вшитый OS+Basic).
    // RPATH остаётся пустым — враппер ставит autorunCartridge=NO_CART.
    if (rom && size == 0) rom = NULL;

    // регистрируем ROM-буфер под именем (враппер откроет его fopen'ом)
    if (rom && rom_name && rom_name[0]) {
        size_t n = strlen(rom_name);
        if (n >= sizeof(g_path)) n = sizeof(g_path) - 1;
        memcpy(g_path, rom_name, n);
        g_path[n] = 0;
    } else if (rom) {
        strcpy(g_path, "game.xex");
    } else {
        g_path[0] = 0;   // BASIC: файла нет, RPATH пустой
    }
    a8_fs_register(g_path, rom, size);

    a8_retro_set_environment(host_environment);
    a8_retro_set_video_refresh(host_video);
    a8_retro_set_audio_sample(host_audio_sample);
    a8_retro_set_audio_sample_batch(host_audio_sample_batch);
    a8_retro_set_input_poll(host_input_poll);
    a8_retro_set_input_state(host_input_state);

    a8_retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = (g_path[0] ? g_path : NULL);
    info.data = (void*)rom;   // ядро игнорирует data, но оставим для порядка
    info.size = size;

    if (!a8_retro_load_game(&info)) {
        printf("A8: load failed\n");
        a8_retro_deinit();
        return;
    }
    printf("A8: started (%s, %u bytes)\n", g_path[0] ? g_path : "BASIC", (unsigned)size);

    emu_set_border_color(0x00061428);   // тёмно-синий (A8)
    emu_throttle_reset();
    emu_esc_hold_reset();

    for (;;) {
        a8_retro_run();
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) break;
    }

    a8_retro_unload_game();
    a8_retro_deinit();
    fb_clear(); fb_flush();
}

// rom_browser вызывает emu_run_atari800 (объявлен в emu.h)