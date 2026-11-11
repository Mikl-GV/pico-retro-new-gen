// c64_host.c — Commodore 64/128 host for H3 bare-metal.
//
// Ядро: h3_bare/cores/vice (vendored libretro/vice-libretro, VICE 3.10,
// EMUTYPE=x128: один бинарь покрывает C64 и C128). retrodep/ — готовый
// libretro-архслой (видео/звук/ввод/архитектура); BIOS C64/C128 вшиты
// (USE_EMBEDDED, include/embedded/*.h).
//
// Мы — тонкий libretro-фронтенд: определяем retro_* колбэки, вызываем
// vice_retro_init/load_game/run, кадр RGB565 856×312 (retro_bmp) пишем сразу
// в HDMI FB 1024×600 (как cpc_host, масштаб по aspect_ratio ядра).
//
// Ввод: USB-клавиатура (HID -> RETROK, событийно через retro_keyboard_callback,
// который регистрирует само ядро в SET_KEYBOARD_CALLBACK — как в cpc r970),
// Sega-геймпад/кнопочный пад (RETRO_DEVICE_JOYPAD, джойстик порта 1/2).
// Звук: 48000 Гц стерео из ядра -> ресемплер (48000->48000, паттерн pce_host),
// push в I2S. Выход: ESC-удержание (emu_esc_hold).
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

// ---- враппер ядра переименован в vice_* (vice_rename.sh) ----
void vice_retro_set_environment(retro_environment_t);
void vice_retro_set_video_refresh(retro_video_refresh_t);
void vice_retro_set_audio_sample(retro_audio_sample_t);
void vice_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void vice_retro_set_input_poll(retro_input_poll_t);
void vice_retro_set_input_state(retro_input_state_t);
void vice_retro_init(void);
bool vice_retro_load_game(const struct retro_game_info*);
void vice_retro_run(void);
void vice_retro_unload_game(void);
void vice_retro_deinit(void);
void vice_retro_get_system_av_info(struct retro_system_av_info*);

// c64_fs.c: регистрация ROM-буфера под путём (для fopen->rfopen)
void c64_fs_register(const char* path, const void* data, uint32_t size);

// ---- ROM: путь из rom_browser (имя файла), буфер в ROM_BUF ----
static char     g_path[160] = "game.d64";
static uint16_t g_joy = 0;
static uint8_t  g_kbd[RETROK_LAST];
// Клавиатура: как cpc r970 — ядро регистрирует SET_KEYBOARD_CALLBACK само,
// host шлёт события на фронтах (g_kbd_prev). Без этого ввод не доходит.
static retro_keyboard_event_t g_kbd_ev;
static uint8_t g_kbd_prev[RETROK_LAST];

// ---- модель (vice_c64_model / vice_c128_model для GET_VARIABLE) ----
// 0 = C64 PAL, 1 = C64 NTSC, 2 = C128 PAL, 3 = C128 NTSC
static int g_c64_model = 0;    // "C64 PAL"
static int g_c128_model = 0;   // "C128 PAL"

void c64_set_model(int m) {
    if (m == 1) { g_c64_model = 1; g_c128_model = 0; }
    else if (m == 3) { g_c64_model = 0; g_c128_model = 1; }
    else { g_c64_model = 0; g_c128_model = 0; }
}

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
    case 0x40: return RETROK_F7;   case 0x41: return RETROK_F8;   case 0x42: return RETROK_F9;
    case 0x43: return RETROK_F10;  case 0x44: return RETROK_F11;  case 0x45: return RETROK_F12;
    case 0x49: return RETROK_INSERT;     case 0x4A: return RETROK_HOME;
    case 0x4B: return RETROK_PAGEUP;     case 0x4C: return RETROK_DELETE;
    case 0x4D: return RETROK_END;        case 0x4E: return RETROK_PAGEDOWN;
    case 0x4F: return RETROK_RIGHT;      case 0x50: return RETROK_LEFT;
    case 0x51: return RETROK_DOWN;       case 0x52: return RETROK_UP;
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
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // Fire (joy)
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);      // Fire 2
    if (sp & 0x0040) j |= (1u << RETRO_DEVICE_ID_JOYPAD_X);      // Space
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);  // Return
    if (sp & 0x0800) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT); // VKBD toggle
    if (sp & 0x0200) j |= (1u << RETRO_DEVICE_ID_JOYPAD_L);      // Run/Stop
    if (sp & 0x0400) j |= (1u << RETRO_DEVICE_ID_JOYPAD_R);      // Reset
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

// ---- звук: стерео 48000 Гц. Ресемплер 48000->48000 (паттерн pce/cpc_host) ----
static size_t host_audio_sample_batch(const int16_t* data, size_t frames)
{
    if (!data || frames == 0) return frames;
    static int16_t out[2048 * 2];
    size_t o = 0;
    // VICE по умолчанию отдаёт 48000 (vice_sound_sample_rate), наш канал тоже
    // 48000 — пары проходят 1:1, но с ограничением порции (ядро может дать
    // больше 2048 за кадр, ресемплерный шаблон пока держит лимит).
    for (size_t i = 0; i < frames && o < 2048; i++) {
        out[2 * o]     = data[2 * i];
        out[2 * o + 1] = data[2 * i + 1];
        o++;
    }
    for (size_t i = 0; i < o; i++)
        i2s_push_sample(out[2 * i], out[2 * i + 1]);
    return frames;
}
static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

// ---- видео: RGB565 856x312 -> HDMI FB 1024x600 напрямую ----
// Масштаб по aspect_ratio ядра (VICE x128: точка не квадратная, как CPT).
#define FB_ADDR_C 0x5F900000u
#define FB_W_C    1024
#define FB_H_C    600
#define C64_BG    0x00060812u   // цвет полей (тёмно-синий, как CPC-бордюр)

static uint32_t* const g_fb = (uint32_t*)FB_ADDR_C;
static int g_dw = 0, g_dh = 0, g_x0 = 0, g_y0 = 0;   // текущая геометрия кадра
static float g_aspect = 0.0f;                        // aspect_ratio ядра (берём из av)

static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;                 // байты -> uint16

    // Картинка на всю высоту, ширина — по aspect_ratio ядра (как cpc_host r970).
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

    // Поля — только при смене геометрии (иначе мерцание).
    if (dw != g_dw || dh != g_dh || x0 != g_x0 || y0 != g_y0) {
        for (int y = 0; y < FB_H_C; y++) {
            uint32_t* row = g_fb + (size_t)y * FB_W_C;
            int inside = (y >= y0 && y < y0 + dh);
            if (!inside) {
                for (int x = 0; x < FB_W_C; x++) row[x] = C64_BG;
            } else {
                for (int x = 0; x < x0; x++) row[x] = C64_BG;
                for (int x = x0 + dw; x < FB_W_C; x++) row[x] = C64_BG;
            }
        }
        fb_dirty_all();   // r1166 (E14): поля перерисованы — dirty = весь экран
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
    // r1166 (E14): host_video пишет HDMI FB напрямую — сообщаем fb_flush
    // перерисованный прямоугольник (поля — только при смене геометрии выше).
    fb_mark_dirty(x0, y0, dw, dh);
    fb_frame_scaled();
}

// ---- environment: ключевые опции ядра ----
// Ошибки/предупреждения ядра печатаем в UART (как cpc r968) — видно
// "Failed to load" и пр.
static void c64_log(enum retro_log_level lvl, const char* fmt, ...) {
    // Порог общий (emu.c:g_core_log_min), по умолчанию WARN: INFO-поток VICE
    // (Set resource, Arg0 и пр.) на блокирующем UART тормозил эмуляцию (r987).
    // Аварии ядра (abort/assert/_exit) с r1212 идут через PANIC в libc_min.c.
    extern int g_core_log_min;
    if ((int)lvl < g_core_log_min) return;
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf(lvl == RETRO_LOG_ERROR ? "  c64# " : "  c64: ");
    if (fmt) {
        va_list ap;
        char buf[512];
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = 0;
        uart0_printf("%s", buf);
    }
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    // VICE сам регистрирует keyboard callback в SET_KEYBOARD_CALLBACK, как
    // cap32 — host его принимает (g_kbd_ev) и шлёт события из host_update_input.
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
        struct retro_keyboard_callback* cb = (struct retro_keyboard_callback*)data;
        if (cb && cb->callback) g_kbd_ev = cb->callback;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = c64_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;   // пути нет — ядро берёт "." / вшитые данные
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        // дефолты опций: 16bpp, crop off, PAL C64/C128, автозапуск дисков.
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "vice_gfx_colors") == 0) { v->value = "16bit"; return true; }
            if (strcmp(v->key, "vice_crop") == 0)       { v->value = "disabled"; return true; }
            if (strcmp(v->key, "vice_autostart") == 0)   { v->value = "enabled"; return true; }
            if (strcmp(v->key, "vice_sound_sample_rate") == 0) { v->value = "48000"; return true; }
            if (strcmp(v->key, "vice_drive_true_emulation") == 0) { v->value = "disabled"; return true; }
            if (strcmp(v->key, "vice_sid_engine") == 0)  { v->value = "Resid"; return true; }
            if (strcmp(v->key, "vice_c64_model") == 0) {
                v->value = g_c64_model == 1 ? "C64 NTSC" : "C64 PAL";
                return true;
            }
            if (strcmp(v->key, "vice_c128_model") == 0) {
                v->value = g_c128_model == 1 ? "C128 NTSC" : "C128 PAL";
                return true;
            }
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

void emu_run_c64(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    // r1012: НЕ ставим g_emu_keep_heap — VICE обязан переинициализироваться
    // с чистого пула. emu_prepare() вызывает gb_heap_reset() (возврат
    // bump-кучи к нулю); free() у gameboy_stubs LIFO-only и при повторном
    // входе пул 24 МБ иначе исчерпывается: второй запуск ROM падал лавиной
    // «lib_malloc/lib_calloc/lib_realloc failed» → «Cannot initialize rombanks
    // resources» → зависание (стенд r1011: 1942.d64 стартует, AIRWOLF2 — нет).
    emu_prepare();
    // r1183: C64 рисует в FB_ADDR напрямую (g_fb), поэтому режим двойной
    // буферизации ему не подходит — возвращаем штатный буфер.
    fb_flip_set(0);
    snd_manifest("c64", "sid");

    // BASIC/ROM: rom==NULL -> вшитый BASIC (KERNAL+Basic), нет файла.
    if (rom && size == 0) rom = NULL;

    if (rom && rom_name && rom_name[0]) {
        size_t n = strlen(rom_name);
        if (n >= sizeof(g_path)) n = sizeof(g_path) - 1;
        memcpy(g_path, rom_name, n);
        g_path[n] = 0;
    } else if (rom) {
        strcpy(g_path, "game.d64");
    } else {
        g_path[0] = 0;
    }

    // r1003: КОРЕНЬ отказа автозапуска — регистр расширения. VICE определяет
    // тип образа через dc_get_image_type() → strendswith(filename, "d64")
    // РЕГИСТРОЗАВИСИМО. Файлы на флешке часто в ВЕРХНЕМ регистре
    // (`1942.D64`, `WREST1.D64`, `BOMB JACK II++.PRG`) → тип UNKNOWN →
    // autostart не включается вовсе, ядро пробует tape/CRT и пишет
    // «not a valid file». Приводим расширение к нижнему регистру (имя файла
    // на носителе не меняется: fopen/stat идут через path_matches, который
    // теперь сравнивает без учёта регистра).
    {
        char* dot = 0;
        for (char* p = g_path; *p; p++)
            if (*p == '.') dot = p;
        if (dot) {
            for (char* p = dot + 1; *p; p++)
                if (*p >= 'A' && *p <= 'Z') *p += 32;
        }
    }
    c64_fs_register(g_path, rom, size);

    // r1000: состояние образа ДО сброса .vice_bss — отделяет «данные не
    // доехали из загрузчика» от «их затёрла инициализация ядра».
    if (rom && size >= 4)
        printf("C64: rom pre-bss=%02X %02X %02X %02X (size=%u)\n",
               rom[0], rom[1], rom[2], rom[3], (unsigned)size);

    // r999: полный сброс BSS ядра VICE перед КАЖДОЙ инициализацией.
    // Секция .vice_bss (NOLOAD, 0x52000000, ~31 МБ) обнуляется только в
    // startup.S на старте прошивки. При повторном входе из меню VICE получал
    // мусор от предыдущего запуска: «error: lib_malloc failed», «unknown
    // resource …», и его структуры затирали ROM_BUF нулями (в логе r998:
    // fread отдавал b0=00 00 00 00 при загруженном образе). Обнуление
    // возвращает ядро в чистое состояние — как при первой загрузке.
    {
        extern uint8_t _vice_bss_start[], _vice_bss_end[];
        size_t vb = (size_t)(_vice_bss_end - _vice_bss_start);
        memset(_vice_bss_start, 0, vb);
        printf("C64: vice_bss cleared %u KB\n", (unsigned)(vb >> 10));
    }

    // Диагностика: образ должен быть цел до инициализации ядра.
    if (rom && size >= 4)
        printf("C64: rom first=%02X %02X %02X %02X (size=%u)\n",
               rom[0], rom[1], rom[2], rom[3], (unsigned)size);

    vice_retro_set_environment(host_environment);
    vice_retro_set_video_refresh(host_video);
    vice_retro_set_audio_sample(host_audio_sample);
    vice_retro_set_audio_sample_batch(host_audio_sample_batch);
    vice_retro_set_input_poll(host_input_poll);
    vice_retro_set_input_state(host_input_state);

    vice_retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = g_path;
    info.data = (void*)rom;
    info.size = size;

    if (!vice_retro_load_game(&info)) {
        printf("C64: load failed\n");
        vice_retro_deinit();
        return;
    }
    printf("C64: started (%s, %u bytes)\n", g_path[0] ? g_path : "BIOS/BASIC", (unsigned)size);

    // aspect_ratio ядра — по нему считается ширина картинки (как cpc r970).
    {
        struct retro_system_av_info av;
        memset(&av, 0, sizeof(av));
        vice_retro_get_system_av_info(&av);
        if (av.geometry.aspect_ratio > 0.1f)
            g_aspect = av.geometry.aspect_ratio;
        else if (av.geometry.base_height > 0)
            g_aspect = (float)av.geometry.base_width / (float)av.geometry.base_height;
    }

    emu_set_border_color(0x00060812);
    // r989: VICE — поцикловая эмуляция, 50 Гц PAL (C64) / 60 Гц NTSC (C128).
    // Дефолт emu_prepare 16667 (60 Гц) заставлял throttle «догонять» 60 Гц при
    // реальных ~33 fps → переработка копилась, CPU гонялся на 100%, USB-поллы
    // клавиатуры прижимались (после выхода — «мёртвое» меню). Ставим честный
    // период 50 Гц: throttle ждёт period только если ядро успело, переработки
    // нет — и нагрузка на OHCI не пиковая.
    emu_set_period_us(20000);   // 50 Гц PAL (C64/C128)
    emu_throttle_reset();
    emu_esc_hold_reset();

    for (;;) {
        vice_retro_run();
        emu_throttle();
        fb_flush();   // host_video уже пишет в HDMI FB (1024x600)
        if (emu_esc_hold()) break;
    }

    // r993: сброс «всего, что передавали ядру». Пока ядро ещё живо, отпускаем
    // ему все клавиши, которые host держал нажатыми (по g_kbd_prev), и обнуляем
    // собственное состояние ввода. Иначе после выхода из C64 у меню/следующего
    // эмулятора остаётся «мусорное» удержание — клавиатура ведёт себя неверно.
    if (g_kbd_ev) {
        for (unsigned k = 0; k < RETROK_LAST; k++)
            if (g_kbd_prev[k]) g_kbd_ev(false, k, 0, 0);
    }
    memset(g_kbd, 0, sizeof(g_kbd));
    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));
    g_joy = 0;

    vice_retro_unload_game();
    vice_retro_deinit();
    fb_clear(); fb_flush();
}