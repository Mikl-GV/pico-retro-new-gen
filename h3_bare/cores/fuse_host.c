// fuse_host.c — ZX Spectrum (Fuse) host for H3 bare-metal.
//
// Ядро: h3_bare/cores/fuse/ (fuse + libspectrum + vendored libretro-common).
// Переиспользуем враппер src/libretro.c; его libretro-API и libretro-common
// символы переименованы objcopy в fuse_* (см. fuse_rename.sh), поэтому зовём
// fuse_retro_*.
//
// Меню: выбор модели (строка OPT display) -> «ROM» (снапшот .z80/.sna) или
// «BASIC» (без контента, машина грузит BASIC). Модель передаётся через
// fuse_set_model() (env отдаёт её как опцию fuse_machine).
//
// Видео: RGB565, окно soft_width×soft_height внутри канвы hard_width (по умолч.
// 320×240 в 320×288) -> EMU_FB 320×240 -> emu_scale. Timex (640×480) сжимаем.
// Ввод: USB-клавиатура -> RETROK_*; Sega-пад -> JOYPAD (Kempston).
// Звук: отключён.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "libretro.h"

#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "fb_text.h"
#include "h3_hs_timer.h"
#include "i2s.h"
#include "uart.h"

// fuse_retro_* — переименованный враппер (fuse_rename.sh)
void fuse_retro_set_environment(retro_environment_t);
void fuse_retro_set_video_refresh(retro_video_refresh_t);
void fuse_retro_set_audio_sample(retro_audio_sample_t);
void fuse_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void fuse_retro_set_input_poll(retro_input_poll_t);
void fuse_retro_set_input_state(retro_input_state_t);
void fuse_retro_init(void);
bool fuse_retro_load_game(const struct retro_game_info*);
void fuse_retro_run(void);
void fuse_retro_unload_game(void);
void fuse_retro_deinit(void);
/* r0.392: результат загрузки контента (см. libretro.c) */
extern int fuse_content_load_ok;
/* r750: сброс накопленного виртуального сна Fuse между сессиями
 * (compat/timer.c: slept_ms). Дрейф времени при повторных запусках ZX. */
extern void fuse_timer_reset(void);

#define EMU_FB_ADDR 0x5F800000u
#define EMU_FB_W    320
#define EMU_FB_H    240

// ---- Модель (выбор из меню) ----
static const char* g_model = "Spectrum 48K";
void fuse_set_model(const char* m) { if (m && m[0]) g_model = m; }

// Список опций ядра (retro_variable[], отданный через SET_VARIABLES —
// статичен в ядре, валиден весь сеанс).
static const struct retro_variable* g_core_vars = 0;

// r0.226: вернуть ДЕФОЛТ опции (первое значение после "; ") из списка ядра.
// Раньше отдавали "" — ядро делает atoi("")=0 для fuse_emulation_speed →
// эмуляция на 0% → звук вне диапазона → some_audio не ставится → retro_run
// выжигает 10000 итераций на кадр («застрял на первом экране»).
// r0.410 (S10): один static buf[64] на все опции был опасен, если ядро
// задержит чтение v->value — указатель менялся следующей опцией. Теперь
// кольцо из 24 буферов (список опций Fuse ~20), указатели живут до конца
// сессии. Если ядро-клиент снова попросит >24 дефолтов — кольцо переживёт
// только последние 24; держать список большего размера Fuse не требует.
static const char* core_default_for(const char* key)
{
    static char pool[24][64];
    static int   pool_i = 0;
    if (!g_core_vars) return "";
    for (const struct retro_variable* v = g_core_vars; v->key; v++) {
        if (strcmp(v->key, key) == 0) {
            const char* p = strchr(v->value, ';');
            if (!p) return "";
            p++;
            while (*p == ' ') p++;
            char* buf = pool[pool_i];
            pool_i = (pool_i + 1) % 24;
            size_t n = 0;
            while (p[n] && p[n] != '|' && n < 63) n++;
            memcpy(buf, p, n);
            buf[n] = 0;
            return buf;
        }
    }
    return "";
}

// ---- ROM (для ROM-варианта) ----
static const void* g_rom = 0;
static uint32_t    g_rom_size = 0;
static char        g_rom_path[128] = "game.z80";

// ---- Ввод ----
static uint16_t g_joy = 0;                  // RETRO_DEVICE_ID_JOYPAD биты
static uint8_t  g_kbd[RETROK_LAST];         // RETROK -> нажата

// r0.220: выход по ESC-удержанию — из ТОГО ЖЕ отчёта, что читает ядро
// (у Low-Speed донгла повторный usb_kbd_get_raw за кадр может отдать пусто,
// и emu_esc_hold не накапливает 900 мс — паттерн msx_exit_req, r0.208).
static int      g_fuse_exit_req = 0;
static uint32_t g_fuse_esc_t0   = 0;

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

static void host_update_input(void)
{
    memset(g_kbd, 0, sizeof(g_kbd));

    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    for (int i = 0; i < n; i++) {
        uint16_t rk = hid_to_retrok(keys[i]);
        if (rk) g_kbd[rk] = 1;
    }

    // ESC-удержание (~0.9 с) из этого же отчёта (r0.220)
    {
        int esc = 0;
        for (int i = 0; i < n; i++) if (keys[i] == 0x29) esc = 1;
        uint32_t now = h3_hs_timer_lo_us();
        if (esc) {
            if (!g_fuse_esc_t0) g_fuse_esc_t0 = now;
            else if (now - g_fuse_esc_t0 > 900000u) g_fuse_exit_req = 1;
        } else {
            g_fuse_esc_t0 = 0;
        }
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) g_kbd[RETROK_LSHIFT] = 1;
    if (mods & 0x20) g_kbd[RETROK_RSHIFT] = 1;
    if (mods & 0x01) g_kbd[RETROK_LCTRL] = 1;
    if (mods & 0x10) g_kbd[RETROK_RCTRL] = 1;
    if (mods & 0x04) g_kbd[RETROK_LALT] = 1;
    if (mods & 0x40) g_kbd[RETROK_RALT] = 1;

    uint16_t j = 0;
    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // A = Fire
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
    if (sp & 0x0040) j |= (1u << RETRO_DEVICE_ID_JOYPAD_X);
    if (sp & 0x0100) j |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_L);      // Start = Enter
    if (sp & 0x0800) j |= (1u << RETRO_DEVICE_ID_JOYPAD_R);      // Mode = Space
    g_joy = j;
}

// ---- libretro callbacks ----
static void host_input_poll(void) { host_update_input(); }

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    // r0.223: ядро Fuse скан-клавиатуру делает на ПОРТУ 2 (retro_init ставит
    // port0=Cursor, port1=Kempston, port2=Spectrum Keyboard в Src/libretro.c).
    // Раньше отвечали 0 на всё, кроме port==0 → клавиатура не передавалась
    // вообще («первый экран и всё», 128K не выбирается).
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    if (port > 1) return 0;
    if (device == RETRO_DEVICE_JOYPAD)
        return (g_joy & (1u << id)) ? 1 : 0;
    return 0;
}

static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t host_audio_sample_batch(const int16_t* d, size_t f)
{
    // r608: звук ZX Spectrum (Fuse) на I2S. Ядро отдаёт стерео пары int16
    // 44100 Гц (compat/sound.c: sound_lowlevel_frame → audio_cb(data,len/2)).
    // Ресемпл 44100→48000 (линейный, фаза 16.16, шаг 60211), push в I2S.
    if (!d || f == 0) return f;
    static uint32_t rs_phase = 0;
    static int16_t out[2048];   // максимум выходных стерео-пар за вызов
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
        rs_phase = 0;
    for (size_t i = 0; i < o; i++)
        i2s_push_sample(out[2 * i], out[2 * i + 1]);
    return f;
}

// Кадр RGB565 (pitch в байтах) -> EMU_FB 320x240 (с downscale для Timex 640x480).
static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    uint16_t* dst = (uint16_t*)EMU_FB_ADDR;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;

    if (width <= EMU_FB_W && height <= EMU_FB_H) {
        for (unsigned y = 0; y < height; y++)
            memcpy(dst + (size_t)y * EMU_FB_W, src + (size_t)y * sp, width * sizeof(uint16_t));
    } else {
        // Timex 640x480 -> 320x240 (nearest)
        for (unsigned y = 0; y < EMU_FB_H; y++) {
            unsigned sy = (y * height) / EMU_FB_H;
            const uint16_t* srow = src + (size_t)sy * sp;
            uint16_t* drow = dst + (size_t)y * EMU_FB_W;
            for (unsigned x = 0; x < EMU_FB_W; x++)
                drow[x] = srow[(x * width) / EMU_FB_W];
        }
    }
}

static void host_log(enum retro_log_level level, const char* fmt, ...)
{
    // r0.417: тихий лог (UART не спамим). Диагностика «загрузки ROM» (r735)
    // снята — подтверждено на стенде: .z80/.sna/.tap/.dsk грузятся штатно.
    (void)level; (void)fmt;
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "fuse_machine") == 0) {
                v->value = g_model;
            } else if (strcmp(v->key, "fuse_auto_machine") == 0) {
                // r0.382: дефолт ядра — disabled; из-за него 128K-снапшоты
                // (.z80 v3/.sna 128K) не грузились на выбранной 48K-модели.
                v->value = "enabled";
            } else {
                // r0.226: дефолт из списка ядра (не ""), см. core_default_for
                v->value = core_default_for(v->key);
            }
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: { bool* b = (bool*)data; if (b) *b = false; return true; }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* l = (struct retro_log_callback*)data;
        if (l) l->log = host_log;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        g_core_vars = (const struct retro_variable*)data;   // r0.226: дефолты опций
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:      return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:       return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:             return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:          return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:    return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:      return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:              return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:          return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:     return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:    return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: { unsigned* v = (unsigned*)data; if (v) *v = 1; return true; }
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: { unsigned* v = (unsigned*)data; if (v) *v = 1; return true; }
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION: { unsigned* v = (unsigned*)data; if (v) *v = 1; return true; }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:     return false;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:        return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:             { bool* b = (bool*)data; if (b) *b = true; return true; }
    default: return false;
    }
}

// ---- выход по ESC-удержанию (как у остальных ядер) ----

// r0.392: регистронезависимое сравнение суффикса (без POSIX strcasecmp)
static int fuse_ext_eq(const char* dot, const char* ext)
{
    const char *a = dot, *b = ext;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

// r0.392: расширения, которые ядро Fuse умеет распознавать (valid_extensions
// в libretro.c: tzx|tap|z80|rzx|scl|trd|dsk|dck|sna|szx|ipf|zip|m3u).
// Сырые .rom/.bin и прочие файлы ядро «не распознаёт» и МОЛЧА остаётся в
// BASIC — поэтому до загрузки выводим понятное предупреждение.
static int fuse_ext_supported(const char* path)
{
    const char* dot = strrchr(path, '.');
    if (!dot) return 0;
    static const char* const exts[] = {
        ".tzx", ".tap", ".z80", ".rzx", ".scl", ".trd", ".dsk",
        ".dck", ".sna", ".szx", ".ipf", ".zip", ".m3u"
    };
    for (unsigned i = 0; i < sizeof(exts)/sizeof(exts[0]); i++)
        if (fuse_ext_eq(dot, exts[i])) return 1;
    return 0;
}

// r0.392: RAM выбранной модели для предупреждения «файл не влезает в память».
// 0 = модель с расширенной памятью / неизвестна — проверку пропускаем.
static uint32_t fuse_model_ram(const char* model)
{
    if (!model) return 0;
    if (strstr(model, "16K")) return 16384;
    if (strstr(model, "48K")) return 49152;
    if (strstr(model, "128K") || strstr(model, "+2") || strstr(model, "+3")
        || strstr(model, "Pentagon") || strstr(model, "Scorpion")) return 0;
    /* Timex TC2048/TC2068/TS2068 и SE — есть 48K/128K, проверку пропускаем */
    if (strstr(model, "Timex") || strstr(model, "SE")) return 0;
    return 0;
}

void emu_run_fuse(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    emu_prepare();
    snd_manifest("zxspectrum", "ay8912");

    g_rom = rom; g_rom_size = size;
    if (rom_name && rom_name[0]) {
        snprintf(g_rom_path, sizeof(g_rom_path), "%s", rom_name);
    } else {
        snprintf(g_rom_path, sizeof(g_rom_path), "game.z80");
    }

    printf("FUSE: %s: %s (%u bytes)\n", g_model,
           (rom && size) ? g_rom_path : "BASIC", (unsigned)size);

    // r0.392: понятный отказ до входа в эмулятор, если формат не ZX.
    if (rom && size && !fuse_ext_supported(g_rom_path)) {
        printf("FUSE: unsupported format '%s'\n", g_rom_path);
        fb_clear();
        fb_text_center("FUSE: format not recognized", 200, 2, 0x00FF4444);
        fb_text_center(g_rom_path, 240, 2, 0x00FFFFFF);
        fb_text_center(".z80/.sna/.szx/.tap/.tzx/.dsk/.scl/.trd/.dck/.ipf/.zip", 258, 1, 0x00AAAAAA);
        fb_flush();
        udelay(2500000);
        fb_clear(); fb_flush();
        return;
    }

    // r0.392: «не влезает в память» — для сырых образов (не снапшот/лента)
    // больше RAM выбранной модели. Снапшоты .z80/.sna несут свою модель
    // (auto_machine переключает), ленты в RAM не грузятся целиком.
    if (rom && size) {
        const char* dot = strrchr(g_rom_path, '.');
        uint32_t ram = fuse_model_ram(g_model);
        int is_mem_hungry = 1;
        if (dot) {
            static const char* const ext_ok[] = {
                ".z80", ".sna", ".szx", ".tap", ".tzx", ".dsk",
                ".scl", ".trd", ".dck", ".rzx", ".m3u"
            };
            for (unsigned i = 0; i < sizeof(ext_ok)/sizeof(ext_ok[0]); i++)
                if (fuse_ext_eq(dot, ext_ok[i])) { is_mem_hungry = 0; break; }
        }
        if (ram && is_mem_hungry && size > ram) {
            printf("FUSE: WARN %s (%u) > RAM модели %s (%u)\n",
                   g_rom_path, (unsigned)size, g_model, (unsigned)ram);
            fb_clear();
            fb_text_center("FUSE: file bigger than model RAM", 200, 2, 0x00FFAA00);
            char buf[96];
            snprintf(buf, sizeof(buf), "%s: %u > %u байт", g_model, (unsigned)size, (unsigned)ram);
            fb_text_center(buf, 240, 1, 0x00FFFFFF);
            fb_text_center("choose 128K model in menu or another file", 258, 1, 0x00AAAAAA);
            fb_flush();
            udelay(2500000);
        }
    }

    fuse_retro_set_environment(host_environment);
    fuse_retro_set_video_refresh(host_video);
    fuse_retro_set_audio_sample(host_audio_sample);
    fuse_retro_set_audio_sample_batch(host_audio_sample_batch);
    fuse_retro_set_input_poll(host_input_poll);
    fuse_retro_set_input_state(host_input_state);

    fuse_timer_reset();   // r750: сброс виртуального сна перед новой сессией

    fuse_retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    if (rom && size) {                 // ROM (снапшот)
        info.path = g_rom_path;
        info.data = rom;
        info.size = size;
    } else {                           // BASIC (без контента)
        info.path = NULL;
        info.data = NULL;
        info.size = 0;
    }

    if (!fuse_retro_load_game(&info)) {
        printf("FUSE: load failed\n");
        fuse_retro_deinit();
        return;
    }
    i2s_dc_shift_set(6);   // r608: ZX — непрерывный поток → ~120 Гц

    // r0.392: ядро могло «успешно» стартовать в пустой BASIC, не распознав
    // контент. Показываем причину и возвращаемся в меню (после короткой
    // паузы, чтобы сообщение успели увидеть на экране).
    if (rom && size && !fuse_content_load_ok) {
        printf("FUSE: content not loaded (%u bytes)\n", (unsigned)size);
        fuse_retro_unload_game();
        fuse_retro_deinit();
        fb_clear();
        fb_text_center("FUSE: file not loaded", 200, 2, 0x00FF4444);
        fb_text_center(g_rom_path, 240, 2, 0x00FFFFFF);
        fb_text_center("format not recognized (bad header)", 258, 1, 0x00AAAAAA);
        fb_flush();
        udelay(2000000);
        fb_clear(); fb_flush();
        return;
    }

    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();
    g_fuse_exit_req = 0;
    g_fuse_esc_t0 = 0;

    for (;;) {
        fuse_retro_run();
        emu_throttle();
        emu_scale(EMU_FB_W, EMU_FB_H);
        fb_flush();
        if (emu_esc_hold() || g_fuse_exit_req) break;
    }

    fuse_retro_unload_game();
    fuse_retro_deinit();
    fb_clear(); fb_flush();
}
