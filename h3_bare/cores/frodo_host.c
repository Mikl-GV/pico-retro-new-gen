// frodo_host.c — Commodore 64 host for H3 bare-metal.
//
// Ядро: h3_bare/cores/frodo (vendored r-type/frodo-libretro, Frodo V4_2,
// базовый Frodo — честный C64, BASIC V2, PAL/NTSC). BIOS вшиты в ядро
// (Src/Basic_ROM.h, Kernal_ROM.h, Char_ROM.h, 1541_ROM.h). libco НЕ
// используем: NO_LIBCO — синхронный retro_run() без переключения контекста.
//
// Мы — тонкий libretro-фронтенд: определяем retro_* колбэки, вызываем
// frodo_retro_init/load_game/run, кадр XRGB8888 384x288 (Retro_Screen) пишем
// напрямую в HDMI FB 1024x600 (масштаб по aspect_ratio ядра).
//
// Ввод: USB-клавиатура (HID -> RETROK, опрос через input_state_cb, который
// ядро само опрашивает в Process_key/Retro_PollEvent), Sega-геймпад/кнопочный
// пад (RETRO_DEVICE_JOYPAD, джойстик порта 1/2).
// Звук: 44100 Гц моно из ядра (audio_cb, по фрейму) -> ресемплер
// 44100->48000 (низкозатратный линейный), push в I2S. Выход: ESC-удержание.
// Рекорд: образ диска (.d64/.t64/x64/p00/zip) регистрируем через fs_register
// под тем же путём, что передаём в info.path — Frodo открывает его fopen'ом
// в драйвере привода (ImageDrive/ArchDrive), без SD: буфер из ROM_BUF.
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

// ---- враппер ядра переименован в frodo_* (frodo_rename.sh) ----
void frodo_retro_set_environment(retro_environment_t);
void frodo_retro_set_video_refresh(retro_video_refresh_t);
void frodo_retro_set_audio_sample(retro_audio_sample_t);
void frodo_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void frodo_retro_set_input_poll(retro_input_poll_t);
void frodo_retro_set_input_state(retro_input_state_t);
void frodo_retro_init(void);
bool frodo_retro_load_game(const struct retro_game_info*);
void frodo_retro_run(void);
void frodo_retro_unload_game(void);
void frodo_retro_deinit(void);
void frodo_retro_get_system_av_info(struct retro_system_av_info*);

// Единый файловый слой: fs_register заставляет fopen(путь) отдавать ROM-буфер.
void fs_register(const char* path, const void* data, uint32_t size);

// ---- ROM: путь из rom_browser (имя файла), буфер в ROM_BUF ----
static char     g_path[160] = "game.d64";
static uint16_t g_joy = 0;
static uint8_t  g_kbd[RETROK_LAST];
static uint8_t  g_kbd_prev[RETROK_LAST];

// ---- ввод: HID -> RETROK (тот же маппинг, что в a8/fuse/c64) ----
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
    // Джойстик порта 2 — зеркало порта 1 (одно устройство на стенде).
    if (port == 1 && device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return (int16_t)g_joy;
        return (g_joy & (1u << id)) ? 1 : 0;
    }
    return 0;
}

// ---- звук: 44100 Гц моно из ядра -> 48000 Гц стерео в I2S ----
// Frodo в retro_run гонит audio_cb(mono, mono) 882 раза (44100/50) каждый кадр.
// Собираем кадр во входной буфер и ресемплим 882->960 (48000/50) линейно.
#define FR_IN    882   // входных моно-фреймов за кадр (44100/50)
#define FR_OUT   (48000 / 50)     // 960 выходных стерео-фреймов за кадр (48000/50)
static int16_t g_ain[FR_IN];
static int     g_ain_n = 0;

static void host_audio_sample(int16_t l, int16_t r)
{
    (void)r;
    if (g_ain_n < FR_IN) g_ain[g_ain_n++] = l;
    if (g_ain_n >= FR_IN) {
        // Линейный ресемплер 44100->48000 (коэфф 160/147).
        static int16_t out[FR_OUT * 2];
        int pos = 0;
        for (int i = 0; i < FR_OUT; i++) {
            int s0 = (int)i * FR_IN / FR_OUT;
            int s1 = s0 + 1; if (s1 >= FR_IN) s1 = FR_IN - 1;
            int frac = (i * FR_IN * 1000 / FR_OUT) % 1000;
            int32_t v = g_ain[s0] + ((g_ain[s1] - g_ain[s0]) * frac / 1000);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[pos++] = (int16_t)v;
            out[pos++] = (int16_t)v;
        }
        // r1326 (A1): пакетная подача ОДНИМ i2s_push_block вместо цикла
        // push_sample (dsb на пару, ~960 барьеров/кадр) — как gba_mgba/32x/gpgx.
        i2s_push_block(out, FR_OUT);
        g_ain_n = 0;
    }
}
static size_t host_audio_batch(const int16_t* data, size_t frames)
{
    for (size_t i = 0; i < frames; i++)
        host_audio_sample(data[2 * i], data[2 * i + 1]);
    return frames;
}

// ---- видео: XRGB8888 384x288 -> HDMI FB 1024x600 напрямую ----
#define FB_ADDR_C 0x5F900000u
#define FB_W_C    1024
#define FB_H_C    600
#define C64_BG    0x00060812u   // цвет полей (тёмно-синий, как CPC-бордюр)

static uint32_t* const g_fb = (uint32_t*)FB_ADDR_C;
static int g_dw = 0, g_dh = 0, g_x0 = 0, g_y0 = 0;   // текущая геометрия кадра
static float g_aspect = 4.0f / 3.0f;                 // aspect_ratio ядра

static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;
    const uint32_t* src = (const uint32_t*)data;
    size_t sp = pitch >> 2;                 // байты -> uint32

    // Картинка на всю высоту, ширина — по aspect_ratio ядра (4:3).
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
        fb_dirty_all();
        g_dw = dw; g_dh = dh; g_x0 = x0; g_y0 = y0;
    }

    // Таблица исходных колонок (16.16), XRGB8888 -> RGB888 напрямую.
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
        const uint32_t* srow = src + (size_t)sy * sp;
        uint32_t* drow = g_fb + (size_t)(y0 + dy) * FB_W_C + x0;
        for (int dx = 0; dx < dw; dx++) {
            uint32_t p = srow[sx[dx]];                 // XRGB8888
            drow[dx] = p;                              // верхний байт (X) не трогаем
        }
        acc_y += step_y;
        int ny = (int)(acc_y >> 16);
        if (ny > sy) { if (ny >= (int)height) ny = (int)height - 1; sy = ny; }
    }
    fb_dirty_all();
    fb_frame_scaled();
}

// ---- environment: ключевые опции ядра ----
static void frodo_log(enum retro_log_level lvl, const char* fmt, ...) {
    extern int g_core_log_min;
    if ((int)lvl < g_core_log_min) return;
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf(lvl == RETRO_LOG_ERROR ? "  frodo# " : "  frodo: ");
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
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        // Frodo просит XRGB8888 (retro_init); RGB565 не принимаем.
        unsigned fmt = data ? *(unsigned*)data : RETRO_PIXEL_FORMAT_UNKNOWN;
        return fmt == RETRO_PIXEL_FORMAT_XRGB8888;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = frodo_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;   // пути нет — ядро берёт "." / вшитые данные
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    default:
        return false;
    }
}

void emu_run_frodo(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    extern int g_core_log_min;
    emu_prepare();
    fb_flip_set(0);
    snd_manifest("c64", "sid");

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

    // Расширение к нижнему регистру (fs_layer сравнивает без учёта регистра,
    // но IsMountableFile в Frodo — регистрозависим; имя файла на носителе
    // не меняем — fopen идёт через path_matches).
    {
        char* dot = 0;
        for (char* p = g_path; *p; p++)
            if (*p == '.') dot = p;
        if (dot) {
            for (char* p = dot + 1; *p; p++)
                if (*p >= 'A' && *p <= 'Z') *p += 32;
        }
    }
    // Грузим образ через единый файловый слой (ROM_BUF, круговая память).
    if (rom && size) fs_register(g_path, rom, size);

    frodo_retro_set_environment(host_environment);
    frodo_retro_set_video_refresh(host_video);
    frodo_retro_set_audio_sample(host_audio_sample);
    frodo_retro_set_audio_sample_batch(host_audio_batch);
    frodo_retro_set_input_poll(host_input_poll);
    frodo_retro_set_input_state(host_input_state);

    frodo_retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    // r1285: для BASIC путь пуст — Frodo не должен пробовать монтировать
    // несуществующий "game.d64" (давал fs-промахи и лишние ветки привода).
    info.path = g_path;
    info.data = (void*)rom;
    info.size = size;

    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    frodo_retro_get_system_av_info(&av);
    if (av.geometry.aspect_ratio > 0.1f)
        g_aspect = av.geometry.aspect_ratio;
    else if (av.geometry.base_height > 0)
        g_aspect = (float)av.geometry.base_width / (float)av.geometry.base_height;
    else
        g_aspect = 4.0f / 3.0f;

    int log_save = g_core_log_min;
    g_core_log_min = 1;
    // r1285: границы входа/выхода load_game в логе — если «begin» есть, а
    // «end» нет, значит управление не вернулось из инициализации ядра.
    printf("FRODO: load_game begin ('%s')\n", info.path ? info.path : "");
    bool loaded = frodo_retro_load_game(&info);
    printf("FRODO: load_game end (loaded=%d)\n", (int)loaded);
    g_core_log_min = log_save;

    if (!loaded) {
        printf("FRODO: load failed (core not built)\n");
        frodo_retro_unload_game();
        frodo_retro_deinit();
        emu_audio_stop();
        return;
    }
    printf("FRODO: started (%s, %u bytes)\n", g_path[0] ? g_path : "BIOS/BASIC", (unsigned)size);

    emu_set_border_color(0x00060812);
    emu_set_period_us(20000);   // 50 Гц PAL (C64)
    emu_throttle_reset();
    emu_esc_hold_reset();

    uint32_t perf_t0 = h3_hs_timer_lo_us();
    uint32_t perf_frames = 0;
    for (;;) {
        frodo_retro_run();
        emu_throttle();
        fb_flush();
        perf_frames++;
        if (emu_esc_hold()) {
            // r1285: причина и момент выхода — без этого повторные входы
            // выглядели в логе как «самопроизвольный рестарт ядра».
            printf("FRODO exit: esc/pad hold at frame=%u\n", (unsigned)perf_frames);
            break;
        }
    }
    {
        uint32_t el = h3_hs_timer_lo_us() - perf_t0;
        if (el && perf_frames)
            printf("FRODO perf: fps=%.1f frames=%u\n",
                   (double)perf_frames * 1000000.0 / (double)el, (unsigned)perf_frames);
    }

    memset(g_kbd, 0, sizeof(g_kbd));
    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));
    g_joy = 0;

    frodo_retro_unload_game();
    frodo_retro_deinit();
    emu_audio_stop();
    fb_clear(); fb_flush();
}