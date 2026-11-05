// cpc_host.c — Amstrad CPC host for H3 bare-metal.
//
// Ядро: h3_bare/cores/cap32 (vendored libretro-cap32, Caprice32 4.2.0).
// Мы — тонкий libretro-фронтенд: определяем retro_* колбэки, вызываем
// cpc_retro_init/load_game/run, кадр RGB565 (768×272, DEPTH_16BPP) из host_video
// пишем сразу в HDMI FB 1024×600 (апскейл ×4/3, поля сверху/снизу).
//
// BIOS всех моделей вшиты в ядро (cap32/rom/464.h, 6128.h, 664.h, 6128p.h) —
// внешний образ не нужен. ROM/диск (SNA/DSK/TAP/CPR) грузятся через cpc_fs
// (rfopen поверх зарегистрированного буфера ROM_BUF).
//
// Ввод: USB-клавиатура (HID -> RETROK, полная карта CPC через keyboard_translation),
// Sega-геймпад/кнопочный пад (RETRO_DEVICE_JOYPAD). Звук: стерео 44100 Гц ->
// ресемпл 44100->48000 (паттерн pce_host.c), push в I2S.
//
// Выход: ESC-удержание (emu_esc_hold).
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
#include "i2s.h"
#include "h3_hs_timer.h"

// ---- враппер ядра переименован в cpc_* (cpc_rename.sh) ----
void cpc_retro_set_environment(retro_environment_t);
void cpc_retro_set_video_refresh(retro_video_refresh_t);
void cpc_retro_set_audio_sample(retro_audio_sample_t);
void cpc_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void cpc_retro_set_input_poll(retro_input_poll_t);
void cpc_retro_set_input_state(retro_input_state_t);
void cpc_retro_init(void);
bool cpc_retro_load_game(const struct retro_game_info*);
void cpc_retro_run(void);
void cpc_retro_unload_game(void);
void cpc_retro_deinit(void);
void cpc_retro_get_system_av_info(struct retro_system_av_info*);

// cpc_fs.c: регистрация ROM-буфера под путём (для rfopen)
void cpc_fs_register(const char* path, const void* data, uint32_t size);

// ---- ROM: путь из rom_browser (имя файла), буфер в ROM_BUF ----
static char     g_path[160] = "game.sna";
static uint16_t g_joy = 0;
static uint8_t  g_kbd[RETROK_LAST];
// r970: cap32 получает клавиатуру ТОЛЬКО через retro_keyboard_event_t,
// который ядро регистрирует в RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK
// (retro_events.c:754). Раньше мы этот вызов игнорировали — ввод не доходил
// вообще. g_kbd_prev — чтобы звать callback только на фронтах.
static retro_keyboard_event_t g_kbd_ev;
static uint8_t g_kbd_prev[RETROK_LAST];

// ---- ввод: HID -> RETROK (тот же маппинг, что в a8/fuse/bk) ----
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
    case 0x3A: return RETROK_F1;   case 0x3B: return RETROK_F2;   case 0x3C: return RETROK_F3;
    case 0x3D: return RETROK_F4;   case 0x3E: return RETROK_F5;   case 0x3F: return RETROK_F6;
    case 0x40: return RETROK_F7;   case 0x41: return RETROK_F8;
    // r993: F9 (VKEYB) и F10 (GUI) НЕ мапим — ядро cap32 по ним открывает
    // собственный libretro-UI (retro_ui_toggle_status UI_KEYBOARD/UI_MENU),
    // которого наш host не поддерживает: рендер UI уходит в мусорные
    // указатели → Data Abort (дамп F1..F6 из startup.S), эмулятор падает.
    case 0x44: return RETROK_F11;  case 0x45: return RETROK_F12;
    case 0x49: return RETROK_INSERT;     case 0x4A: return RETROK_HOME;
    case 0x4B: return RETROK_PAGEUP;     case 0x4C: return RETROK_DELETE;
    case 0x4D: return RETROK_END;        case 0x4E: return RETROK_PAGEDOWN;
    case 0x4F: return RETROK_RIGHT;      case 0x50: return RETROK_LEFT;
    case 0x51: return RETROK_DOWN;       case 0x52: return RETROK_UP;
    // Keypad: у CPC это F0..F9 + INTRO, поэтому маппим полностью.
    case 0x53: return RETROK_KP_PERIOD;
    case 0x54: return RETROK_KP_DIVIDE;  case 0x55: return RETROK_KP_MULTIPLY;
    case 0x56: return RETROK_KP_MINUS;   case 0x57: return RETROK_KP_PLUS;
    case 0x58: return RETROK_KP_ENTER;
    case 0x59: return RETROK_KP1;  case 0x5A: return RETROK_KP2;  case 0x5B: return RETROK_KP3;
    case 0x5C: return RETROK_KP4;  case 0x5D: return RETROK_KP5;  case 0x5E: return RETROK_KP6;
    case 0x5F: return RETROK_KP7;  case 0x60: return RETROK_KP8;  case 0x61: return RETROK_KP9;
    case 0x62: return RETROK_KP0;
    default: return 0;
    }
}

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

    uint16_t sp = pad_scan_combined();
    uint16_t j = 0;
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // Fire 1 (Amstrad A)
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);      // Fire 2 (Amstrad B)
    if (sp & 0x0040) j |= (1u << RETRO_DEVICE_ID_JOYPAD_X);      // Fire 3 (Amstrad C)
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);  // (Amstrad D)
    if (sp & 0x0800) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT);
    g_joy = j;

    // События клавиатуры ядру (frontend обязан звать callback сам).
    if (g_kbd_ev) {
        for (unsigned k = 0; k < RETROK_LAST; k++) {
            if (g_kbd[k] != g_kbd_prev[k]) {
                unsigned ch = 0;
                if (g_kbd[k]) {   // character — только для печатных клавиш
                    if (k >= RETROK_a && k <= RETROK_z)
                        ch = (unsigned)('a' + (k - RETROK_a));
                    else if (k >= RETROK_1 && k <= RETROK_9)
                        ch = (unsigned)('1' + (k - RETROK_1));
                    else if (k == RETROK_0) ch = '0';
                    else if (k == RETROK_SPACE) ch = ' ';
                    else if (k == RETROK_RETURN) ch = '\r';
                    if (ch && (g_kbd[RETROK_LSHIFT] || g_kbd[RETROK_RSHIFT]) && ch >= 'a' && ch <= 'z')
                        ch -= 32;
                }
                g_kbd_ev(g_kbd[k] ? true : false, k, ch, 0);
            }
        }
    }
    memcpy(g_kbd_prev, g_kbd, sizeof(g_kbd));
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

// ---- звук: стерео 44100 Гц -> 48000 (паттерн pce_host.c) ----
static size_t host_audio_sample_batch(const int16_t* data, size_t frames)
{
    if (!data || frames == 0) return frames;
    static uint32_t rs_phase = 0;
    static int16_t  cpc_tail[2] = { 0, 0 };
    static int16_t out[2048 * 2];
    const size_t in_n = frames;
    size_t o = 0;
    while (o < 2048) {
        uint32_t i = rs_phase >> 16;
        if (i >= (uint32_t)in_n) break;
        uint32_t fr = rs_phase & 0xFFFFu;
        int32_t l0, r0, l1, r1;
        if (i == 0)      { l0 = cpc_tail[0]; r0 = cpc_tail[1]; }
        else             { l0 = data[2 * (i - 1)];     r0 = data[2 * (i - 1) + 1]; }
        if (i + 1 <= in_n) { l1 = data[2 * (i + 1 - 1)];     r1 = data[2 * (i + 1 - 1) + 1]; }
        else break;
        out[2 * o]     = (int16_t)(l0 + (int32_t)(((int64_t)(l1 - l0) * (int32_t)fr) >> 16));
        out[2 * o + 1] = (int16_t)(r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)fr) >> 16));
        o++;
        rs_phase += 60211u;
    }
    if (in_n > 0) { cpc_tail[0] = data[2 * (in_n - 1)]; cpc_tail[1] = data[2 * (in_n - 1) + 1]; }
    rs_phase &= 0xFFFFu;
    for (size_t i = 0; i < o; i++)
        i2s_push_sample(out[2 * i], out[2 * i + 1]);
    return frames;
}
static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

// ---- видео: RGB565 -> HDMI FB 1024x600 напрямую ----
// r968: вывод пишется СРАЗУ в FB (XRGB8888), минуя EMU_FB 320x240 — без
// децимации 768->320 (она давала «полосы» и мерцание чересстрочных строк).
// r970: масштаб АНИЗОТРОПНЫЙ по aspect_ratio ядра (24/17): у CPC пиксель
// не квадратный. Раньше считали по пикселям кадра (768:272 = 2.8:1) —
// картинка выглядела сильно растянутой по горизонтали. Теперь высота
// заполняет экран, ширина = 600*aspect ≈ 847 (поля по 88 px).
// Поля заливаются ТОЛЬКО при смене геометрии (иначе мерцание).
#define FB_ADDR_C 0x5F900000u
#define FB_W_C    1024
#define FB_H_C    600
#define CPC_BG    0x00060812u   // цвет полей (тёмно-синий бордюр CPC)

static uint32_t* const g_fb = (uint32_t*)FB_ADDR_C;
static int g_dw = 0, g_dh = 0, g_x0 = 0, g_y0 = 0;   // текущая геометрия кадра
static float g_aspect = 24.0f / 17.0f;               // aspect_ratio ядра (см. emu_run_cpc)

static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;                 // байты -> uint16

    // r970: картинка на всю высоту, ширина — по aspect_ratio ЯДРА (24/17),
    // а не по пикселям кадра. Пиксель CPC не квадратный, поэтому
    // 768:272 (2.8:1) на экране выглядел растянутым по горизонтали.
    // Обрезки нет — свободная ширина уходит в поля.
    int dh = FB_H_C;
    int dw = (int)((float)dh * g_aspect + 0.5f);
    if (dw > FB_W_C) {
        dw = FB_W_C;
        dh = (int)((float)dw / g_aspect + 0.5f);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dh > FB_H_C) dh = FB_H_C;
    int x0 = (FB_W_C - dw) / 2;
    int y0 = (FB_H_C - dh) / 2;

    // Поля — только при смене геометрии (см. комментарий выше).
    if (dw != g_dw || dh != g_dh || x0 != g_x0 || y0 != g_y0) {
        for (int y = 0; y < FB_H_C; y++) {
            uint32_t* row = g_fb + (size_t)y * FB_W_C;
            int inside = (y >= y0 && y < y0 + dh);
            if (!inside) {
                for (int x = 0; x < FB_W_C; x++) row[x] = CPC_BG;
            } else {
                for (int x = 0; x < x0; x++) row[x] = CPC_BG;
                for (int x = x0 + dw; x < FB_W_C; x++) row[x] = CPC_BG;
            }
        }
        g_dw = dw; g_dh = dh; g_x0 = x0; g_y0 = y0;
    }

    // Таблица исходных колонок (16.16): один раз на смену ширины/масштаба.
    static int      sx[FB_W_C];
    static unsigned sx_src = 0;
    static int      sx_dst = 0;
    if (width != sx_src || dw != sx_dst) {
        uint32_t step = ((uint32_t)width << 16) / (uint32_t)dw;
        uint32_t acc  = step >> 1;
        for (int dx = 0; dx < dw; dx++) {
            sx[dx] = (int)(acc >> 16);
            acc += step;
            if (acc >= ((uint32_t)width << 16)) acc -= (uint32_t)width << 16;
        }
        sx_src = width; sx_dst = dw;
    }

    uint32_t step_y = ((uint32_t)height << 16) / (uint32_t)dh;
    uint32_t acc_y  = step_y >> 1;
    int sy = 0;
    for (int dy = 0; dy < dh; dy++) {
        const uint16_t* srow = src + (size_t)sy * sp;
        uint32_t* drow = g_fb + (size_t)(y0 + dy) * FB_W_C + x0;
        for (int dx = 0; dx < dw; dx++) {
            uint16_t p = srow[sx[dx]];
            drow[dx] = ((uint32_t)((p >> 11) & 0x1Fu) << 19)
                     | ((uint32_t)((p >>  5) & 0x3Fu) << 10)
                     | ((uint32_t)( p        & 0x1Fu) <<  3);
        }
        acc_y += step_y;
        int ny = (int)(acc_y >> 16);
        if (ny > sy) { if (ny >= (int)height) ny = (int)height - 1; sy = ny; }
    }
}

// ---- environment: ключевые опции ядра ----
// r968: ошибки ядра (не найдена/битая DSK, SNA, проблема модели) печатаем в
// UART — раньше весь лог глушился и «остался BASIC» никак не диагностировался.
static void cpc_log(enum retro_log_level lvl, const char* fmt, ...) {
    if (lvl < RETRO_LOG_WARN) return;
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf(lvl == RETRO_LOG_ERROR ? "  cpc# " : "  cpc: ");
    if (fmt) uart0_printf("%s", fmt);
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    // r970: без этого вызова ядро не получает НИ ОДНОЙ клавиши:
    // cap32 передаёт ввод только через этот callback (см. g_kbd_ev).
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
        struct retro_keyboard_callback* cb = (struct retro_keyboard_callback*)data;
        if (cb && cb->callback) g_kbd_ev = cb->callback;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = cpc_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        // дефолты: 16bpp, crop off, модель по умолчанию ядра (6128)
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "cap32_gfx_colors") == 0) { v->value = "16bit"; return true; }
            if (strcmp(v->key, "cap32_scr_crop") == 0)   { v->value = "disabled"; return true; }
            if (strcmp(v->key, "cap32_autorun") == 0)    { v->value = "enabled"; return true; }
            if (strcmp(v->key, "cap32_model") == 0)      { v->value = "6128"; return true; }
            if (strcmp(v->key, "cap32_ram") == 0)        { v->value = "128"; return true; }
            if (strcmp(v->key, "cap32_tape_fastload") == 0) { v->value = "disabled"; return true; }
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    default:
        return false;
    }
}

void emu_run_cpc(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    // r1015: НЕ ставим g_emu_keep_heap — cap32 не кэширует указатели пула
    // между запусками (pbRAM/pbROM пересоздаются в init, retro_free в
    // deinit). Симптом (лог r1014): CPC (единственный перед A8) с keep_heap=1
    // НЕ давал gb_heap_reset() — пул 24 МБ оставался занят капри32 после
    // выхода; следующий запуск Atari 800 падал «Fatal error: out of memory»
    // (Util_malloc → malloc NULL), хотя файл читался нормально.
    emu_prepare();
    snd_manifest("cpc", "ay-3-8912");

    // BASIC/ROM: rom==NULL -> вшитый BIOS + BASIC (нет файла).
    if (rom && size == 0) rom = NULL;

    if (rom && rom_name && rom_name[0]) {
        size_t n = strlen(rom_name);
        if (n >= sizeof(g_path)) n = sizeof(g_path) - 1;
        memcpy(g_path, rom_name, n);
        g_path[n] = 0;
    } else if (rom) {
        strcpy(g_path, "game.dsk");
    } else {
        g_path[0] = 0;
    }
    cpc_fs_register(g_path, rom, size);

    cpc_retro_set_environment(host_environment);
    cpc_retro_set_video_refresh(host_video);
    cpc_retro_set_audio_sample(host_audio_sample);
    cpc_retro_set_audio_sample_batch(host_audio_sample_batch);
    cpc_retro_set_input_poll(host_input_poll);
    cpc_retro_set_input_state(host_input_state);

    cpc_retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = g_path;
    info.data = (void*)rom;
    info.size = size;

    if (!cpc_retro_load_game(&info)) {
        printf("CPC: load failed\n");
        cpc_retro_deinit();
        return;
    }
    printf("CPC: started (%s, %u bytes)\n", g_path[0] ? g_path : "BIOS/BASIC", (unsigned)size);

    // r970: aspect_ratio ядра (cap32 задаёт 24/17) — по нему считается ширина
    // картинки. Без этого кадр 768x272 растягивался по горизонтали.
    {
        struct retro_system_av_info av;
        memset(&av, 0, sizeof(av));
        cpc_retro_get_system_av_info(&av);
        if (av.geometry.aspect_ratio > 0.1f)
            g_aspect = av.geometry.aspect_ratio;
        else if (av.geometry.base_height > 0)
            g_aspect = (float)av.geometry.base_width / (float)av.geometry.base_height;
    }

    emu_set_border_color(0x00060812);
    emu_throttle_reset();
    emu_esc_hold_reset();

    for (;;) {
        cpc_retro_run();
        emu_throttle();
        fb_flush();   // host_video уже пишет в HDMI FB (1024x600)
        if (emu_esc_hold()) break;
    }

    cpc_retro_unload_game();
    cpc_retro_deinit();
    fb_clear(); fb_flush();
}