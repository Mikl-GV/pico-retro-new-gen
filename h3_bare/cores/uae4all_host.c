// uae4all_host.c — host-слой Commodore Amiga (UAE4All / libretro-порт Chips).
// ЗАМЕНА puae_host.c (r1243): PUAE заменён на UAE4All (см. HANDOVER r1243).
// Подключает UAE4All к bare-metal H3: ADF-дискеты с SD/USB через fs_layer
// (fs_register), видео RGB565 (gfx_mem 320x240) -> EMU_FB -> HDMI,
// звук 44100->48000 (ресемплер как в puae_host), ввод — Sega-геймпад
// (D-Pad + fire) + USB-клавиатура.
//
// Жизненный цикл (как r1231 для PUAE): ядро держим инициализированным между
// входами (тёплый вход: только unload/load), первый вход — pre_main+init.
//   вход:   uae4all_pre_main(path) -> real_main (self-init) -> цикл run();
//   выход:  uae4all_leave_program (НЕ deinit, тёплый перезапуск).
//
// Kickstart вшит (правило владельца — BIOS/ROM не с SD):
//   bios/kick34005.A500 — Kickstart 1.3 через fs_layer (второй файл слоя).
//   GET_SYSTEM_DIRECTORY отдаёт ".", чтобы ядро собирало "./kick34005.A500".
//
// Видео: ядро рисует в gfx_mem (320x240 RGB565) и вызывает video_cb в конце
//   retro_run (uae4all_retro_video). host эмулирует libretro-колбэки.

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "libretro.h"

#include "emu.h"
#include "fb_text.h"
#include "h3.h"
#include "h3_hs_timer.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"

// ---- враппер ядра переименован в u4a_* (uae4all_rename.sh) ----
// ВАЖНО: uae4all — C++ (libretro-обвязка Chips), имена mangled. Определения
// снаружи должны быть extern "C" — иначе линкер ищет u4a__Z...
#ifdef __cplusplus
extern "C" {
#endif
// libretro-интерфейс
void u4a_retro_set_environment(retro_environment_t);
void u4a_retro_set_video_refresh(retro_video_refresh_t);
void u4a_retro_set_audio_sample(retro_audio_sample_t);
void u4a_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void u4a_retro_set_input_poll(retro_input_poll_t);
void u4a_retro_set_input_state(retro_input_state_t);
void u4a_retro_init(void);
void u4a_retro_deinit(void);
void u4a_retro_reset(void);
void u4a_retro_run(void);
bool u4a_retro_load_game(const struct retro_game_info* info);
void u4a_retro_unload_game(void);
unsigned u4a_retro_get_region(void);
void u4a_retro_get_system_av_info(struct retro_system_av_info* info);
void u4a_retro_get_system_info(struct retro_system_info* info);

// ядро UAE4All (C-обёртки, см. libretro-core.cpp: u4a_*_c)
void u4a_pre_main_c(const char* argv);
void u4a_leave_program_c(void);
int  u4a_Retro_PollEvent_c(void);
extern int u4a_libretroreset;
extern char* u4a_gfx_mem;
extern unsigned u4a_gfx_rowbytes;
extern int u4a_retrow, u4a_retroh;
extern int u4a_JOYSTICK_EMULATED;
extern int u4a_KEYBOARD_PROCESS;
extern int u4a_pauseg;
extern char u4a_Key_State[512];
extern unsigned g_fs_rom_opens;
#ifdef __cplusplus
}
#endif

// ---- собственные глобалы (host) ----
static unsigned g_audio_run = 0;

// r1248: физическая мышь/тачпад для порта 0. Ядро спрашивает её через
// RETRO_DEVICE_MOUSE (core-mapper.cpp: rmouse_x/y -> lastmx/lastmy), а без
// этой ветки lastmx/lastmy не менялись — курсор Amiga стоял на месте.
static int16_t g_mouse_dx = 0, g_mouse_dy = 0;
static uint8_t g_mouse_l = 0, g_mouse_r = 0;

// ---- единый лог (как puae_log) ----
extern int g_core_log_min;         // 0=DEBUG..3=ERROR (emu.c)
static void u4a_log(enum retro_log_level lvl, const char* fmt, ...) {
    if ((int)lvl < g_core_log_min) return;
    extern int uart0_printf(const char* fmt, ...);
    static const char tag[] = { 'D', 'I', 'W', 'E' };
    unsigned t = (unsigned)lvl;
    uart0_printf("  u4a%c# ", tag[t < 4 ? t : 3]);
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

// ---- environment: переменные ядра + системная директория ----
static bool u4a_environment(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        // ядро ставит RGB565 (RENDER16B в libretro-core.h)
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = u4a_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        // Вшитая прошивка: Kickstart регистрируется в fs_layer под "." 
        // (ведущий "./" слой отбрасывает), ядро склеит "./kick34005.A500".
        if (data) *(const char**)data = ".";
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            // Жёсткие настройки (правило владельца, как tex r1235):
            // throttle = none (максимально), frameskip = 0, chip 512KB,
            // statusbar вкл, joystick вкл, keyboard вкл, vpos default.
            if (strcmp(v->key, "uae4all_throttle") == 0)  { v->value = "none"; return true; }
            if (strcmp(v->key, "uae4all_frameskip") == 0) { v->value = "0";    return true; }
            if (strcmp(v->key, "uae4all_memchip") == 0)   { v->value = "default"; return true; }
    if (strcmp(v->key, "uae4all_vpos") == 0)      { v->value = "0";    return true; }
    if (strcmp(v->key, "uae4all_statusbar") == 0) { v->value = "off";  return true; }
            if (strcmp(v->key, "uae4all_joystick") == 0)  { v->value = "on";   return true; }
            if (strcmp(v->key, "uae4all_keyboard") == 0)  { v->value = "on";   return true; }
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    default:
        return false;
    }
}

// ---- звук: стерео 44100 -> 48000 (шаг 60211, ресемплер как puae_host) ----
static size_t u4a_audio_sample_batch(const int16_t* d, size_t f) {
    if (!d || f == 0) return f;
    static uint32_t rs_phase = 0;
    static int16_t out[2048 * 2];
    const size_t in_n = f;
    size_t o = 0;
    while (o < 2048) {
        uint32_t i = rs_phase >> 16;
        if (i + 1 >= (uint32_t)in_n) break;
        uint32_t fr = rs_phase & 0xFFFFu;
        int32_t l0 = d[2 * i],     l1 = d[2 * (i + 1)];
        int32_t r0 = d[2 * i + 1], r1 = d[2 * (i + 1) + 1];
        out[2 * o]     = (int16_t)(l0 + (int32_t)(((int64_t)(l1 - l0) * (int32_t)fr) >> 16));
        out[2 * o + 1] = (int16_t)(r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)fr) >> 16));
        o++;
        rs_phase += 60211u;
    }
    if (rs_phase >= ((uint64_t)in_n << 16))
        rs_phase = (uint32_t)(rs_phase - ((uint64_t)in_n << 16));
    else
        rs_phase &= 0xFFFFu;
    for (size_t i = 0; i < o; i++)
        i2s_push_sample(out[2 * i], out[2 * i + 1]);
    return f;
}

static void u4a_audio_sample(int16_t l, int16_t r) {
    int16_t d[2] = { l, r };
    (void)u4a_audio_sample_batch(d, 1);
}

// ---- видео: RGB565 320x240 -> EMU_FB -> HDMI ----
static void u4a_video(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data || width == 0 || height == 0) return;
    // r1252: Amiga PAL — 4:3 при неквадратном пикселе. Пропорциональный скейл
    // 320x256 давал 1.25 и текст выглядел сжатым по X; тянем по аспекту 4:3.
    emu_scale_rgb565_src_aspect((const uint16_t*)data, (int)width, (int)height,
                                (int)(pitch >> 1), 4, 3);
}

// ---- ввод: Sega-геймпад + USB-клава через libretro состояния ----
// r1243: используем единый pad_scan_combined (Sega 6-btn OR кнопочный пад);
// клавиатура — HID-сканкод -> RETROK, как в puae_host.c hid_to_retrok.
static uint8_t g_kbd[RETROK_LAST];
static uint16_t g_kbd_prev[RETROK_LAST];
static int g_kbd_event = 0;

// HID-сканкод -> RETROK (та же таблица, что в puae_host.c:129)
static uint16_t hid_to_retrok(uint8_t sc) {
    switch (sc) {
    case 0x04: return RETROK_a;   case 0x05: return RETROK_b;   case 0x06: return RETROK_c;
    case 0x07: return RETROK_d;   case 0x08: return RETROK_e;   case 0x09: return RETROK_f;
    case 0x0a: return RETROK_g;   case 0x0b: return RETROK_h;   case 0x0c: return RETROK_i;
    case 0x0d: return RETROK_j;   case 0x0e: return RETROK_k;   case 0x0f: return RETROK_l;
    case 0x10: return RETROK_m;   case 0x11: return RETROK_n;   case 0x12: return RETROK_o;
    case 0x13: return RETROK_p;   case 0x14: return RETROK_q;   case 0x15: return RETROK_r;
    case 0x16: return RETROK_s;   case 0x17: return RETROK_t;   case 0x18: return RETROK_u;
    case 0x19: return RETROK_v;   case 0x1a: return RETROK_w;   case 0x1b: return RETROK_x;
    case 0x1c: return RETROK_y;   case 0x1d: return RETROK_z;
    case 0x1e: return RETROK_1;   case 0x1f: return RETROK_2;   case 0x20: return RETROK_3;
    case 0x21: return RETROK_4;   case 0x22: return RETROK_5;   case 0x23: return RETROK_6;
    case 0x24: return RETROK_7;   case 0x25: return RETROK_8;   case 0x26: return RETROK_9;
    case 0x27: return RETROK_0;
    case 0x28: return RETROK_RETURN; case 0x29: return RETROK_ESCAPE;
    case 0x2a: return RETROK_BACKSPACE; case 0x2b: return RETROK_TAB;
    case 0x2c: return RETROK_SPACE;  case 0x2d: return RETROK_MINUS;
    case 0x2e: return RETROK_EQUALS; case 0x2f: return RETROK_LEFTBRACKET;
    case 0x30: return RETROK_RIGHTBRACKET; case 0x31: return RETROK_BACKSLASH;
    case 0x33: return RETROK_SEMICOLON; case 0x34: return RETROK_QUOTE;
    case 0x35: return RETROK_BACKQUOTE;  case 0x36: return RETROK_COMMA;
    case 0x37: return RETROK_PERIOD;     case 0x38: return RETROK_SLASH;
    case 0x39: return RETROK_CAPSLOCK;
    case 0x4a: return RETROK_HOME; case 0x4b: return RETROK_UP;
    case 0x4c: return RETROK_PAGEUP; case 0x4d: return RETROK_LEFT;
    case 0x4e: return RETROK_RIGHT;  case 0x4f: return RETROK_END;
    case 0x50: return RETROK_DOWN;   case 0x51: return RETROK_PAGEDOWN;
    case 0x52: return RETROK_INSERT; case 0x53: return RETROK_DELETE;
    case 0x54: return RETROK_SYSREQ;
    // r1251: функциональные клавиши (Amiga F1..F10 и расширенные F11/F12).
    case 0x3a: return RETROK_F1;   case 0x3b: return RETROK_F2;
    case 0x3c: return RETROK_F3;   case 0x3d: return RETROK_F4;
    case 0x3e: return RETROK_F5;   case 0x3f: return RETROK_F6;
    case 0x40: return RETROK_F7;   case 0x41: return RETROK_F8;
    case 0x42: return RETROK_F9;   case 0x43: return RETROK_F10;
    case 0x44: return RETROK_F11;  case 0x45: return RETROK_F12;
    default:   return RETROK_UNKNOWN;
    }
}

static void u4a_input_poll(void) {
    usb_kbd_poll();
    usb_pad_poll();
    // r1248: тачпад/мышь как порт 0. Позиция абсолютная (0..1023 × 0..599),
    // ядру нужны относительные dx/dy; масштаб 2 счёта на пиксель — как в
    // puae_host (r1237), иначе курсор Amiga еле ползёт.
    static int last_x = 0, last_y = 0, have_last = 0;
    int mx, my;
    if (usb_pad_get_pos(&mx, &my)) {
        if (!have_last) { last_x = mx; last_y = my; have_last = 1; }
        int dx = mx - last_x, dy = my - last_y;
        last_x = mx; last_y = my;
        if (dx > 63) dx = 63; else if (dx < -63) dx = -63;
        if (dy > 63) dy = 63; else if (dy < -63) dy = -63;
        g_mouse_dx = (int16_t)(dx * 2);
        g_mouse_dy = (int16_t)(dy * 2);
    } else {
        g_mouse_dx = 0;
        g_mouse_dy = 0;
    }
    int mb = usb_pad_buttons();
    g_mouse_l = (mb & 1) ? 1 : 0;
    g_mouse_r = (mb & 2) ? 1 : 0;
    // Ядро вызывает input_state МНОГО раз на кадр (по каждому скэнкоду 0..511),
    // поэтому клавиатуру читаем один раз здесь, в poll.
    uint8_t raw[8]; int n;
    n = usb_kbd_get_raw(raw, 8);   // 6KRO: модификаторы + 6 кодов
    memset(g_kbd, 0, sizeof(g_kbd));
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            uint16_t rk = hid_to_retrok(raw[i]);
            if (rk < RETROK_LAST) g_kbd[rk] = 1;
        }
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x01) g_kbd[RETROK_LCTRL] = 1;
    if (mods & 0x02) g_kbd[RETROK_LSHIFT] = 1;
    if (mods & 0x04) g_kbd[RETROK_LALT] = 1;
    if (mods & 0x08) g_kbd[RETROK_LMETA] = 1;   // Super/Windows (GUI)
    if (mods & 0x10) g_kbd[RETROK_RCTRL] = 1;
    if (mods & 0x20) g_kbd[RETROK_RSHIFT] = 1;
    if (mods & 0x40) g_kbd[RETROK_RALT] = 1;
    if (mods & 0x80) g_kbd[RETROK_RMETA] = 1;   // Super/Windows (GUI)
}

static int16_t u4a_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    // r1251: порты НЕ зеркалим. Ядро (core-mapper.cpp) на порту 0 ждёт мышь и
    // хоткеи (SELECT=vkbd, START=мышь/джой, L/R=смена диска), на порту 1 —
    // джойстик (read_joystick(1) -> joy0dir). При зеркалировании A/B порта 0
    // читались как кнопки мыши, и каждое нажатие переключало режим
    // «joystick -> mouse -> joystick» (в логе — поток Switch to ...).
    if (port > 1) return 0;
    // Джойстик: D-Pad + fire (B/A). Маска как sega_pad_scan:
    //   UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 START=0x80 SELECT=0x0800
    if (device == RETRO_DEVICE_JOYPAD) {
        static uint16_t cached = 0;   // скан один раз за кадр
        static uint32_t last_t = 0;
        uint32_t now = h3_hs_timer_lo_us();
        if (now - last_t > 5000) {   // 5 мс кэш — хватает на кадр
            cached = pad_scan_combined();
            last_t = now;
        }
        // Порт 0 — только хоткеи ядра: направления и A/B отдаём на порту 1,
        // иначе пад одновременно «жмёт мышь» и «жмёт джойстик».
        // r1255: START НЕ отдаём — ядро трактует его как переключатель
        // MOUSE_EMULATED, и каждое нажатие сбивало режим (в логах поток
        // «Switch to mouse emulation / Switch-off»); мышь работает всегда,
        // джойстик включается огнём на порту 1, обратно — кликом мыши.
        if (port == 0) {
            switch (id) {
            case RETRO_DEVICE_ID_JOYPAD_SELECT:return (cached & 0x0800) ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_L:     return (cached & 0x0100) ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_R:     return (cached & 0x0200) ? 1 : 0;
            default: return 0;
            }
        }
        // r1254: геймпад не всегда найден (в логе "sega_pad: init FAIL"),
        // поэтому дублируем управление с клавиатуры: стрелки — крестовина,
        // Space — fire1, Ctrl/Alt — fire2. Иначе в играх «нет направлений».
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:
            return ((cached & 0x0001) || g_kbd[RETROK_UP])    ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:
            return ((cached & 0x0002) || g_kbd[RETROK_DOWN])  ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:
            return ((cached & 0x0004) || g_kbd[RETROK_LEFT])  ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:
            return ((cached & 0x0008) || g_kbd[RETROK_RIGHT]) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_B:     // A = fire1
            return ((cached & 0x0010) || g_kbd[RETROK_SPACE]) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_A:     // B = fire2
            return ((cached & 0x0020) || g_kbd[RETROK_LCTRL] ||
                    g_kbd[RETROK_LALT]) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START: return (cached & 0x0080) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT:return (cached & 0x0800) ? 1 : 0;
        default: return 0;
        }
    }
    // Мышь — только порт 0 (иначе ядро читает её дважды)
    if (device == RETRO_DEVICE_MOUSE && port != 0)
        return 0;
    // Клавиатура: g_kbd заполнен в u4a_input_poll (RETROK-сканкоды)
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    // r1248: мышь порта 0 (core-mapper.cpp читает её, когда MOUSE_EMULATED != 1)
    if (device == RETRO_DEVICE_MOUSE) {
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X:     return g_mouse_dx;
        case RETRO_DEVICE_ID_MOUSE_Y:     return g_mouse_dy;
        case RETRO_DEVICE_ID_MOUSE_LEFT:  return (int16_t)g_mouse_l;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return (int16_t)g_mouse_r;
        default: return 0;
        }
    }
    return 0;
}

// ---- вшитый Kickstart: регистрация в fs_layer (как puae_host) ----
extern void a8_fs_register(const char* path, const void* data, uint32_t size);
extern void a8_fs_register_bios(const char* path, const void* data, uint32_t size);

// ---- точка входа host (зовётся из emu.c; имена замены PUAE, меню не трогаем) ----
// Модель: ХОЛОДНЫЙ init на каждый вход. UAE4All.init лёгкий и сам делает всё
// заново в pre_main -> real_main (memory_init/disk/custom — без тёплых
// статик-кэшей PUAE). Пул ядра — глобальный gb_heap (мalloc/calloc), который
// emu_prepare() сбрасывает ДО инициализации — арена не растёт между сессиями.
void emu_run_amiga(const uint8_t* rom, uint32_t size, const char* rom_name) {
    extern void* _sbrk(int incr);
    extern int g_emu_keep_sbrk;
    g_emu_keep_sbrk = 0;   // sbrk можно сбрасывать (ядро не держит кучи)

    emu_prepare();          // gb_heap_reset + i2s_ring_reset + период 16667

    // --- регистрация колбэков libretro ДО инициализации ядра ---
    u4a_retro_set_environment(u4a_environment);
    u4a_retro_set_video_refresh(u4a_video);
    u4a_retro_set_audio_sample(u4a_audio_sample);
    u4a_retro_set_audio_sample_batch(u4a_audio_sample_batch);
    u4a_retro_set_input_poll(u4a_input_poll);
    u4a_retro_set_input_state(u4a_input_state);
    u4a_retro_init();

    // --- Kickstart: вшит в fs_layer под "./kick34005.A500" ---
    {
        extern void a8_fs_register_bios(const char*, const void*, uint32_t);
        extern const unsigned char puae_kick13_rom[], puae_kick13_rom_end[];
        a8_fs_register_bios("kick34005.A500", puae_kick13_rom,
                            (uint32_t)(puae_kick13_rom_end - puae_kick13_rom));
    }

    // --- образ дискеты: fs_layer под коротким именем df0.adf ---
    // r1254: ядро копирует путь в uae4all_image_file[128] (strncpy 127) и
    // определяет тип по ХВОСТУ пути. Длинное имя рвалось на ".ad", ядро уходило
    // в ветку "-s floppy0=..." (parse_cmdline её не знает -> "skipping unknown
    // option"), диск не монтировался. Короткое имя ещё и совпадает с дефолтом
    // prefs_df[0]="./df0.adf", поэтому DISK_init ставит диск сразу.
    if (rom_name && rom_name[0] && rom) {
        extern void a8_fs_register(const char*, const void*, uint32_t);
        a8_fs_register("df0.adf", rom, size);
        printf("Amiga: fs reg 'df0.adf' <- '%s' size=%u\n",
               rom_name, (unsigned)size);
    }

    emu_set_period_us(20000u);   // PAL 50 Гц

    // retro_load_game(PATH) -> RPATH=PATH -> pre_main -> real_main: полная
    // инициализация (под __LIBRETRO__ real_main только init, без цикла).
    // ВАЖНО: pre_main строит cmdline по глобальному RPATH (не по argv), поэтому
    // путь ДОЛЖЕН идти через retro_load_game — прямой вызов u4a_pre_main_c
    // оставлял RPATH пустым, диск не получал -df0 <путь> (стенд r1243).
    {
        static const struct retro_game_info info_null = { 0 };
        struct retro_game_info gi = info_null;
        char path[512];
        // r1254: ядру всегда отдаём короткое df0.adf (см. регистрацию выше):
        // имя не обрезается до ".ad" и точно попадает в ветку диска (-df0).
        snprintf(path, sizeof(path), "%s", "df0.adf");
        gi.path = path;
        printf("Amiga: UAE4All init ('%s')\n", path);
        u4a_retro_load_game(&gi);
    }

    // ДИАГ (r1244): карта L1 для зоны стека 0x5FF..0x600 — отображена ли?
    // Падение update_adkmasks: DFAR=0x5FFFFC24 (vld1 [sp]), DFSR=0x21,
    // M1 05FF FFFFFFFF. Смотрим запись L1 для 0x5FF00000 и 0x5FE00000.
    {
        uint32_t l0, e1, l1;
        __asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(l0));
        l0 &= ~0x3FFFu;
        e1 = *(volatile uint32_t*)(l0 + 8);
        l1 = e1 & 0xFFFFF000u;
        printf("Amiga DIAG: L0[1]=%08X l1=%08X\n", (unsigned)e1, (unsigned)l1);
        for (int i = 0; i < 4; i++) {
            uint32_t va = 0x5FC00000u + i * 0x200000u;   // 0x5FC..0x5FE..0x600 (2МБ секции)
            uint64_t x = *(volatile uint64_t*)(l1 + (((va >> 21) & 0x1FFu) * 8u));
            printf("  L1[%08X]=%08X:%08X\n", (unsigned)va,
                   (unsigned)(x >> 32), (unsigned)x);
        }
    }

    // r1245-ДИАГ: CRC копий MMU-таблиц до первого кадра. Если после кадра
    // сумма изменится — карту испортило ядро (Data Abort 0x21 = external
    // abort on translation table walk). Норма: CRC не меняется.
    uint32_t crc_pre = mmu_tables_crc();
    mmu_guard_arm();
    printf("Amiga DIAG: mmu-crc pre=%08X guard=%d\n",
           (unsigned)crc_pre, mmu_guard_broken());

    // --- кадровый цикл: retro_run (внутри: PollEvent -> m68k_go -> video) ---
    emu_throttle_reset();
    emu_esc_hold_reset();
    uint32_t t_core = 0, t_video = 0, nframes = 0;
    for (;;) {
        uint32_t t0 = h3_hs_timer_lo_us();
        u4a_retro_run();          // m68k_go + flush_audio + video_cb
        uint32_t t1 = h3_hs_timer_lo_us();
        if (nframes == 0) {
            uint32_t crc_post = mmu_tables_crc();
            printf("Amiga DIAG: mmu-crc post=%08X guard=%d %s\n",
                   (unsigned)crc_post, mmu_guard_broken(),
                   crc_post != crc_pre ? "CHANGED (карту испортило ядро)" : "ok");
        }
        emu_throttle();
        fb_flush();
        uint32_t t2 = h3_hs_timer_lo_us();
        t_core += t1 - t0;
        t_video += t2 - t1;
        nframes++;
        if (emu_esc_hold()) goto exit;
    }
exit:
    if (nframes)
        printf("Amiga perf: fps=%.1f core=%.1fms video=%.1fms rom_opens=%u\n",
               (double)nframes * 1000000.0 / (double)(t_core + t_video),
               (double)t_core / (double)nframes / 1000.0,
               (double)t_video / (double)nframes / 1000.0,
               g_fs_rom_opens);
    // Выход: leave_program (очистка ядра: gfx, sound, memory)
    u4a_leave_program_c();
    emu_audio_stop();
    fb_clear();
    fb_flush();
}

// ---- Workbench 1.3 (вшитый системный диск) ----
void emu_run_amiga_wb(void) {
    extern const unsigned char puae_wb13_adf[], puae_wb13_adf_end[];
    uint32_t n = (uint32_t)(puae_wb13_adf_end - puae_wb13_adf);
    emu_run_amiga(puae_wb13_adf, n, "wb13.adf");
}