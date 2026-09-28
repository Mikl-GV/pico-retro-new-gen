// bk_host.c — BK-0010/0011M (libretro-bk / BK-Terak-Emu, PDP-11) host for H3 bare-metal.
//
// Ядро: h3_bare/cores/bk/ (vendored libretro-bk). API/символы plain retro_*
// конфликтуют с PCE → bk_rename.sh переименовывает их в bk_retro_*.
// ROM БК вшиты в бинарь (bk_roms.c) и отдаются load_rom_file() напрямую.
//
// Видео: RGB565 512×512 (канва, содержимое 512×256) -> EMU_FB 320×240 -> emu_scale.
// Ввод: USB-клавиатура (RETROK, полная БК-раскладка ядра) + Sega-пад (JOYPAD,
// A/B/X/Y = кнопки 1-4). Звук отключён (PSG emu2149 игнор).
// Выход: ESC-удержание (~0.9 с) из ТОГО ЖЕ отчёта, что читает ядро (r0.220).

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "libretro.h"

#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "fb_text.h"
#include "h3_hs_timer.h"

// bk_retro_* — переименованный враппер (bk_rename.sh)
void bk_retro_set_environment(retro_environment_t);
void bk_retro_set_video_refresh(retro_video_refresh_t);
void bk_retro_set_audio_sample(retro_audio_sample_t);
void bk_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void bk_retro_set_input_poll(retro_input_poll_t);
void bk_retro_set_input_state(retro_input_state_t);
void bk_retro_init(void);
bool bk_retro_load_game(const struct retro_game_info*);
void bk_retro_run(void);
void bk_retro_unload_game(void);
void bk_retro_deinit(void);

// r0.257: сброс память гостя/кадра перед загрузкой (см. libretro.c)
void bk_reset_machine(void);

#define EMU_FB_ADDR 0x5F800000u
#define EMU_FB_W    320
#define EMU_FB_H    240

// ---- Модель (выбор из меню, опция bk_model ядра) ----
static const char* g_model = "BK-0010.01";
void bk_set_model(const char* m) { if (m && m[0]) g_model = m; }

// Какие ROM фактически грузит ядро для выбранной модели (libretro.c:734-756).
// Печатаем в UART, чтобы не путать BASIC/FOCAL — раньше всегда писало «BASIC».
static const char* bk_rom_label(const char* m)
{
    if (!m) return "?";
    if (strcmp(m, "BK-0010") == 0)            return "MONIT10+FOCAL10";
    if (strcmp(m, "BK-0010.01") == 0)         return "MONIT10+BASIC10";
    if (strcmp(m, "BK-0010.01 + FDD") == 0)   return "MONIT10+DISK_327";
    if (strcmp(m, "BK-0011M + FDD") == 0)     return "BOS+BAS11M";
    if (strcmp(m, "Slow BK-0011M") == 0)      return "BOS+BAS11M (slow)";
    if (strcmp(m, "Terak 8510/a") == 0)       return "TERAK";
    return "?";
}

// ---- Ввод ----
static uint16_t g_joy = 0;                  // RETRO_DEVICE_ID_JOYPAD биты
static uint8_t  g_kbd[RETROK_LAST];         // RETROK -> нажата

// выход по ESC-удержанию из того же отчёта (паттерн msx/fuse, r0.220)
static int      g_bk_exit_req = 0;
static uint32_t g_bk_esc_t0   = 0;

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
    int n = usb_kbd_get_last(keys, 8);
    for (int i = 0; i < n; i++) {
        uint16_t rk = hid_to_retrok(keys[i]);
        if (rk) g_kbd[rk] = 1;
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) g_kbd[RETROK_LSHIFT] = 1;
    if (mods & 0x20) g_kbd[RETROK_RSHIFT] = 1;
    if (mods & 0x01) g_kbd[RETROK_LCTRL] = 1;
    if (mods & 0x10) g_kbd[RETROK_RCTRL] = 1;

    // ESC-удержание (~0.9 с) из этого же отчёта
    {
        int esc = 0;
        for (int i = 0; i < n; i++) if (keys[i] == 0x29) esc = 1;
        uint32_t now = h3_hs_timer_lo_us();
        if (esc) {
            if (!g_bk_esc_t0) g_bk_esc_t0 = now;
            else if (now - g_bk_esc_t0 > 900000u) g_bk_exit_req = 1;
        } else {
            g_bk_esc_t0 = 0;
        }
    }

    uint16_t j = 0;
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // кнопка 1: A
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);      // кнопка 2: B
    if (sp & 0x0040) j |= (1u << RETRO_DEVICE_ID_JOYPAD_X);      // кнопка 3: X
    if (sp & 0x0100) j |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);      // кнопка 4: Y
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
    if (sp & 0x0800) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT);
    g_joy = j;
}

// ---- libretro callbacks ----
static void host_input_poll(void) { host_update_input(); }

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    if (device == RETRO_DEVICE_JOYPAD && port == 0)
        return (g_joy & (1u << id)) ? 1 : 0;
    return 0;   // mouse не используем
}

static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t host_audio_sample_batch(const int16_t* d, size_t f) { (void)d; return f; }

// r0.255: прямое рисование в HDMI FB (1024×600), НЕ через EMU_FB/emu_scale.
// Кадр ядра 512×512 (Ч/Б: 512 уникальных колонок) при промежуточном
// ресайзе 512→320 терял ~37% колонок → текст «полосами». Здесь X идёт ровно
// в 2× (512→1024, каждый пиксель дублируется — потерь нет), Y 512→600 (≈1.17).
static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;
    unsigned sw = width  ? width  : 512;
    unsigned sh = height ? height : 512;
    uint32_t* dst = (uint32_t*)0x5F900000u;   // HDMI FB XRGB8888 1024×600

    for (unsigned y = 0; y < 600; y++) {
        unsigned sy = (y * sh) / 600u;
        const uint16_t* srow = src + (size_t)sy * sp;
        uint32_t* drow = dst + (size_t)y * 1024u;
        for (unsigned x = 0; x < 1024; x++) {
            uint16_t p = srow[(x * sw) / 1024u];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            drow[x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void host_log(enum retro_log_level level, const char* fmt, ...)
{
    (void)level;
    if (!fmt) return;
    // r0.247: раньше печаталось только fmt -> «%s (%u bytes)» выходило буквально,
    // имён ROM в UART не было. Форматируем через наш vsnprintf (libc_min.c).
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("BKLOG: %s", buf);
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "bk_model") == 0) {
                v->value = g_model;
                return true;
            }
            // r0.253: FOCAL/монитор BK-0010 — монохромные (1 бит/пиксель). Дефолт
            // ядра cflag=1 (color) декодирует их как 2-битный цвет → RGB-мозаика
            // (проверено нативно: colour->красный/зелёный/синий, disabled->белый+чёрный).
            if (strcmp(v->key, "bk_color") == 0) {
                v->value = "disabled";
                return true;
            }
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
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:             return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:       return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:    return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:            return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:      return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:          return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:                 return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:             { bool* b = (bool*)data; if (b) *b = true; return true; }
    default: return false;
    }
}

void emu_run_bk(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    // r0.259: валидация файла программы БК перед загрузкой. Формат —
    // [addr 2B LE][len 2B LE][данные…]. Файлы не в этом формате (оверлеи
    // .OVL/.GMS/.DAT и пр.), попавшие в браузер, грузить нельзя — гость
    // прыгает в мусор и «виснет». Битые — сразу выход в меню без загрузки.
    if (rom && size) {
        if (size < 4) {
            printf("BK: not a loadable program (size=%u < 4)\n", (unsigned)size);
            return;
        }
        unsigned addr = rom[0] | (rom[1] << 8);
        if (addr & 1) {   // нечётный адрес — заведомо не программа (PDP-11 слова)
            printf("BK: bad program address %06o — not a loadable .BIN\n", addr);
            return;
        }
    }

    emu_prepare();

    bk_retro_set_environment(host_environment);
    bk_retro_set_video_refresh(host_video);
    bk_retro_set_audio_sample(host_audio_sample);
    bk_retro_set_audio_sample_batch(host_audio_sample_batch);
    bk_retro_set_input_poll(host_input_poll);
    bk_retro_set_input_state(host_input_state);

    bk_retro_init();

    // r0.257: свежая машина на каждый запуск — обнуляем RAM/ticks/кадр/флаги,
    // иначе при повторном входе (и смене модели) остаётся состояние прошлого прогона.
    bk_reset_machine();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    if (rom && size) {
        info.path = rom_name;
        info.data = rom;
        info.size = size;
    } else {
        info.path = NULL;
        info.data = NULL;
        info.size = 0;
    }

    if (!bk_retro_load_game(&info)) {
        printf("BK: load failed\n");
        bk_retro_deinit();
        return;
    }
    printf("BK: %s [%s]: %s (%u bytes)\n", g_model, bk_rom_label(g_model),
           (rom && size) ? (rom_name ? rom_name : "?") : "baked ROM", (unsigned)size);

    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();
    g_bk_exit_req = 0;
    g_bk_esc_t0 = 0;

    for (;;) {
        bk_retro_run();
        emu_throttle();
        fb_flush();   // r0.255: host_video уже записал HDMI FB напрямую (emu_scale не нужен)
        if (emu_esc_hold() || g_bk_exit_req) break;
    }

    bk_retro_unload_game();
    bk_retro_deinit();
    fb_clear(); fb_flush();
}