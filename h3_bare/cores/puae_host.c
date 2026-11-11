// puae_host.c — host-слой Sega... нет: Commodore Amiga (PUAE / libretro-uae).
// Подключает PUAE (WinUAE 5.3.1) к bare-metal H3: ADF-дискеты с SD через
// fs_layer (a8_fs_register), видео RGB565 из retro_bmp -> EMU_FB 320x240,
// звук 44100->48000 (ресемплер как fuse_host), ввод — Sega-геймпад
// (D-Pad + 2 fire) + USB-клавиатура.
//
// Жизненный цикл (r1231): ядро остаётся инициализированным между входами,
// как смена контента в RetroArch:
//   первый вход: puae_retro_init -> puae_retro_load_game(&info) -> цикл run()
//                -> puae_retro_unload_game (deinit НЕ вызывается);
//   повторный:   emu_prepare без сброса кучи -> load_game -> цикл run().
//
// Kickstart и системный диск вшиты (правило владельца — BIOS/ROM не с SD):
//   bios/kick34005.A500 — Kickstart 1.3 через fs_layer (второй файл слоя);
//   bios/wb13.adf       — Workbench 1.3 для пункта меню emu_run_amiga_wb().
// GET_SYSTEM_DIRECTORY отдаёт ".", чтобы ядро искало Kickstart в слое.

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "libretro.h"

#include "emu.h"
#include "h3_hs_timer.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"

// ---- враппер ядра переименован в puae_* (puae_rename.sh) ----
void puae_retro_set_environment(retro_environment_t);
void puae_retro_set_video_refresh(retro_video_refresh_t);
void puae_retro_set_audio_sample(retro_audio_sample_t);
void puae_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void puae_retro_set_input_poll(retro_input_poll_t);
void puae_retro_set_input_state(retro_input_state_t);
void puae_retro_init(void);
bool puae_retro_load_game(const struct retro_game_info*);
void puae_retro_run(void);
void puae_retro_unload_game(void);
void puae_retro_deinit(void);
void puae_retro_get_system_av_info(struct retro_system_av_info*);
void puae_retro_reset(void);

// ---- путь образа (как c64_host: имя файла с SD) ----
static char g_path[160] = "game.adf";

// ---- кнопки Amiga-джойстика ----
// Джойстик: D-Pad + кнопка → left fire (BTN_A), 2-я кнопка → right/blue (BTN_B).
// Mapping на Sega-паде: крестовина = D-Pad, A = Fire 1, B = Fire 2.
static uint16_t g_joy = 0;
// r1237: мышь Amiga (тачпад). dx/dy считаются за кадр, кнопки — по байту
// boot-mouse; ядро читает их через RETRO_DEVICE_MOUSE (opt_physicalmouse).
static int16_t g_mouse_dx = 0, g_mouse_dy = 0;
static uint8_t g_mouse_l = 0, g_mouse_r = 0, g_mouse_m = 0;

// ---- видео: RGB565 из retro_bmp -> HDMI FB напрямую (emu_scale_rgb565_src).
// Промежуточный EMU_FB 320×240 убран: ядро отдаёт кадр своей геометрии
// (320×256/640×256/640×512, с полями и pitch), и ужатие в 320×240 давало
// «сдвиг растра» на чересстрочных кадрах плюс лишнюю работу каждый кадр.
static int g_geo_printed = 0;
extern unsigned g_fs_read_ops, g_fs_read_bytes, g_fs_rom_opens;   // fs_layer.c: чтения образа

static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;
    if (!g_geo_printed) {
        g_geo_printed = 1;
        printf("Amiga video: %ux%u pitch=%u\n", width, height, (unsigned)pitch);
    }
    // r1236: кадр PUAE (720×574) влезает в HDMI 1024×600 как есть — выводим
    // пиксель в пиксель по центру, без обрезки и без промежуточного 320×240.
    emu_show_rgb565_native((const uint16_t*)data, (int)width, (int)height,
                           (int)(pitch >> 1));
}

// ---- звук: стерео 44100 Гц -> 48000 (шаг 60211, ресемплер как fuse_host) ----
static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t host_audio_sample_batch(const int16_t* d, size_t f)
{
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

// ---- ввод ----
// PUAE опрашивает input_state: RETRO_DEVICE_JOYPAD (D-Pad + B/A) и
// RETRO_DEVICE_KEYBOARD (RETROK_*). Джойстик — из Sega-пада, клавиатура — USB.
static uint8_t g_kbd[RETROK_LAST];
static uint16_t g_kbd_prev[RETROK_LAST];

// HID-сканкод -> RETROK (тот же маппинг, что в c64_host)
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

static void host_input_poll(void)
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
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);   // Fire 1 (A)
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);   // Fire 2 (B)
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START); // Start
    g_joy = j;

    // r1237: тачпад как мышь Amiga. Ядро (opt_physicalmouse) спрашивает
    // RETRO_DEVICE_MOUSE; даём относительные dx/dy от абсолютной позиции
    // тачпада (она в 0..1023 × 0..599, шаг — 4 пикселя за отсчёт HID).
    usb_pad_poll();
    static int last_x = 512, last_y = 300, have_last = 0;
    int mx, my;
    if (usb_pad_get_pos(&mx, &my)) {
        if (!have_last) { last_x = mx; last_y = my; have_last = 1; }
        int dx = mx - last_x, dy = my - last_y;
        last_x = mx; last_y = my;
        // 1 экранный пиксель ≈ 2 счёта мыши Amiga по X и по Y (поле 1024×600).
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
    g_mouse_m = (mb & 4) ? 1 : 0;
}

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    if (device == RETRO_DEVICE_JOYPAD)
        return (g_joy & (1u << id)) ? 1 : 0;
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    // r1237: физическая мышь (тачпад); ядро включает её опцией
    // puae_physicalmouse > 0 и читает здесь dx/dy и кнопки.
    if (device == RETRO_DEVICE_MOUSE) {
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X:      return g_mouse_dx;
        case RETRO_DEVICE_ID_MOUSE_Y:      return g_mouse_dy;
        case RETRO_DEVICE_ID_MOUSE_LEFT:   return (int16_t)g_mouse_l;
        case RETRO_DEVICE_ID_MOUSE_RIGHT:  return (int16_t)g_mouse_r;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (int16_t)g_mouse_m;
        default: return 0;
        }
    }
    return 0;
}

// ---- environment: опции ядра + ключевые настройки ----
// Встроенный AROS-фолбэк (Kickstart не нужен) — правило владельца: BIOS вшит,
// с SD не грузится. Директории возвращаем "/" (fs_layer) — туда ядро пытается
// сходить за kick34005.A500, не находит → AROS.
// Порог логов ядра — общий (emu.c:g_core_log_min). PUAE отдаёт сообщения через
// log_cb с RETRO_LOG_INFO; при пороге WARN они глушились, и старт ядра шёл
// молча. Для отладки Amiga порог поднимается до INFO в emu_run_amiga.
extern int g_core_log_min;

// Ограниченная проба логов: на старте сессии порог поднимается до INFO, но не
// более чем на g_diag_budget строк — дальше автоматически возвращается WARN.
// Так отладочный лог ядра (вставка диска, режимы экрана) не заливает UART и не
// тормозит эмуляцию в обычной работе.
static int g_diag_budget = 0;

static void puae_log(enum retro_log_level lvl, const char* fmt, ...)
{
    if ((int)lvl < g_core_log_min) return;
    if (g_diag_budget > 0 && --g_diag_budget == 0)
        g_core_log_min = RETRO_LOG_WARN;
    extern int uart0_printf(const char* fmt, ...);
    static const char tag[] = { 'D', 'I', 'W', 'E' };
    unsigned t = (unsigned)lvl;
    uart0_printf("  puae%c# ", tag[t < 4 ? t : 3]);
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

extern int uart0_printf(const char* fmt, ...);

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = puae_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        // Каталог нужен ядру, чтобы собрать путь к Kickstart: оно склеивает
        // system_dir + имя и проверяет его через stat (файловый слой). Вшитый
        // KS регистрируется в fs_layer под именем "kick34005.A500", поэтому
        // достаточно "." — ведущие "./" слой отбрасывает.
        if (data) *(const char**)data = ".";
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "puae_model") == 0)    { v->value = "auto"; return true; }
            // Вшитый Kickstart 1.3 rev 34.5 (A500, 256 КБ). Ядро склеивает путь
            // system_dir + это имя и проверяет его через stat — файловый слой
            // отдаёт буфер из flash. AROS остаётся доступен значением "aros".
            if (strcmp(v->key, "puae_kickstart") == 0) { v->value = "kick34005.A500"; return true; }
            if (strcmp(v->key, "puae_fastmem_size") == 0) { v->value = "0"; return true; }
            if (strcmp(v->key, "puae_chipmem_size") == 0) { v->value = "1"; return true; }
            // Slow RAM (0.5 МБ в трапдоле) НЕ включаем: на стенде r1228 она дала
            // Exception 3 (address error) в 0xC000B0 — загрузчик AmigaDOS
            // переезжает в slow RAM и падает. A500 остаётся с 0.5 МБ chip. 
            // Кроп ядра НЕ включаем: с ним оно отдаёт 320×200 при pitch=1440
            // (окно уже полной строки), и на экране видно часть кадра. Поля
            // overscan режим срезает сам в emu_scale_rgb565_src (720×574 -> 640×512).
            if (strcmp(v->key, "puae_crop") == 0) { v->value = "disabled"; return true; }
            // Скорость: по умолчанию ядро шло в «More compatible» 68000
            // ("Building CPU, 44274 opcodes (0 1 1)"), а это самый медленный
            // интерпретатор PUAE. "normal" — обычный, в разы быстрее;
            // «More compatible» нужен лишь отдельным играм.
            if (strcmp(v->key, "puae_cpu_compatibility") == 0) { v->value = "normal"; return true; }
            // Блиттер без ожиданий: быстрее, совместимость достаточна для AROS/игр.
            if (strcmp(v->key, "puae_immediate_blits") == 0) { v->value = "immediate"; return true; }
            // r1237: физическая мышь. Ядро читает RETRO_DEVICE_MOUSE только при
            // opt_physicalmouse > 0; host отдаёт тачпад как dx/dy + кнопки.
            if (strcmp(v->key, "puae_physicalmouse") == 0) { v->value = "1"; return true; }
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

// ---- заглушки dirent (retro_dirent.c исключён: newlib не имеет <dirent.h>).
// PUAE зовёт retro_opendir/retro_readdir для сканирования system-директории
// (список Kickstart) и TEMP-папки ZIP. Ядро PUAЕ переименовано в puae_retro_*
// (puae_rename.sh) — стабы называем так же, иначе голые retro_* конфликтуют
// с такими же символами других ядер (msx_compat.c и др.; linkcheck: дубли).
// В bare-metal системной директории нет (Kickstart вшит/AROS) — «список пуст».
struct RDIR;
struct RDIR* puae_retro_opendir(const char* path) { (void)path; return 0; }
struct RDIR* puae_retro_opendir_include_hidden(const char* path, bool include_hidden) { (void)path; (void)include_hidden; return 0; }
int puae_retro_readdir(struct RDIR* rdir) { (void)rdir; return 0; }
const char* puae_retro_dirent_get_name(struct RDIR* rdir) { (void)rdir; return 0; }
void puae_retro_dirent_get_name_full(struct RDIR* rdir, char* buf, size_t len) { (void)rdir; (void)buf; (void)len; }
bool puae_retro_dirent_is_dir(struct RDIR* rdir, const char* n) { (void)rdir; (void)n; return false; }
void puae_retro_closedir(struct RDIR* rdir) { (void)rdir; }

// ---- POSIX-стабы для newlib: ftello64/fseeko64 объявлены в stdio.h, но не
// реализованы в libc; chmod/truncate отсутствуют (fsdb-код hardfile их зовёт,
// в bare-metal файлы идут через VFS/fs_layer — реальные вызовы не достигают).
_off64_t ftello64(FILE* f) { return (_off64_t)ftello(f); }
_off64_t fseeko64(FILE* f, _off64_t off, int whence) { return (_off64_t)fseeko(f, (_off_t)off, whence); }
int chmod(const char* path, mode_t mode) { (void)path; (void)mode; return 0; }
int truncate(const char* path, off_t length) { (void)path; (void)length; return 0; }

// ---- собственный аллокатор PUAE (puae_malloc/puae_calloc/puae_realloc/puae_free)
// на sbrk-зоне (0x4B700000+), куда пул gameboy_stubs НЕ ходит. Причина:
// глобальные malloc/calloc в проекте — это gb-пул (.gb_heap, 0x4A700000),
// bump-указатель которого на стенде дошёл до 0x4A800000 → Data Abort
// (пересечение зон). puae_rename.sh переименовывает все ссылки malloc/
// calloc/realloc/free в объектах ядра на функции здесь.
// Простой bump + LIFO-free: сессия Amiga живёт от emu_run_amiga до выхода,
// память сбрасывается при повторном входе (sbrk-бросок в emu_prepare).
extern void* _sbrk(int incr);          // libc_min.c (bump, SBRK_LIMIT 0x4EF00000)
// r1234: арена PUAE — bump + список свободных блоков (first-fit). Раньше был
// чистый bump с LIFO-откатом хвоста: освобождённые блоки в середине не
// возвращались, и при тёплом перезапуске ядра арена росла на ~20 МБ за вход
// (стенд r1233: на третьем входе «out of memory for chipmem» → PANIC newcpu.c:2010).
#define PUAE_BLK_MAGIC 0x5041424Cu   // 'PABL'
#define PUAE_HDR_SIZE  32u           // заголовок блока
#define PUAE_MIN_SPLIT 64u           // меньше не делим: осколок бесполезен

typedef struct puae_blk {
    uint32_t magic;
    uint32_t busy;      // 1 = занят, 0 = свободен
    size_t   size;      // запрошенный размер полезной части
    size_t   total;     // полный размер блока с заголовком (кратен 16)
} puae_blk_t;

#define PUAE_PAY(b) ((void*)((char*)(b) + PUAE_HDR_SIZE))

static char* g_puae_brk = 0;
static char* g_puae_base = 0;            // r1229: начало арены (для зануления)
static char* g_puae_brk_low = 0;       // низ арены: откат brk не уходит ниже
static puae_blk_t* g_puae_free = 0;    // список свободных блоков (payload = next)
static int g_core_inited = 0;   // r1231: ядро между входами не выгружается
// r1235: прокрутка арены между сессиями. Слот сдвигает первое выделение на
// PUAE_ROT_STEP, чтобы адреса не повторялись (иначе статические кэши ядра
// считают видео/память уже инициализированными).
#define PUAE_ROT_STEP  (5u * 1024u * 1024u)
#define PUAE_ROT_SLOTS 5u
static unsigned g_rot_slot = 0;

// Список свободных: next лежит в payload (минимум 16 байт — блок ≥ 64, ок).
// Обход ограничен счётчиком: при порче списка лучше потерять блоки, чем висеть.
#define PUAE_WALK_MAX 8192u

static void puae_blk_unlink(puae_blk_t* b)
{
    puae_blk_t** pp = &g_puae_free;
    unsigned guard = 0;
    while (*pp) {
        if (++guard > PUAE_WALK_MAX) { g_puae_free = 0; return; }
        if (*pp == b) { *pp = *(puae_blk_t**)PUAE_PAY(b); return; }
        pp = (puae_blk_t**)PUAE_PAY(*pp);
    }
}

static void puae_blk_push(puae_blk_t* b)
{
    *(puae_blk_t**)PUAE_PAY(b) = g_puae_free;
    g_puae_free = b;
}

static void puae_heap_reset(void)
{
    // r1229: арена перед новой сессией обнуляется. Ядро держит статические
    // указатели (chipmem_bank/bogomem_bank, строки prefs) на прошлую арену:
    // retro_deinit() освобождает не всё, и второй вход иначе читает объекты
    // прошлого запуска как валидные (стенд r1228: чёрный растр на повторе).
    if (g_puae_base && g_puae_brk && g_puae_brk > g_puae_base &&
        g_puae_brk != (char*)(uintptr_t)-1)
        memset(g_puae_base, 0, (size_t)(g_puae_brk - g_puae_base));
    g_puae_brk = 0;
    g_puae_brk_low = 0;
    g_puae_free = 0;
}

void* puae_malloc(size_t sz)
{
    if (sz == 0) sz = 1;
    if (!g_puae_brk) {
        g_puae_brk = (char*)_sbrk(0);
        if (g_puae_brk == (char*)-1) return 0;
        g_puae_brk_low = g_puae_brk;
        if (!g_puae_base) g_puae_base = g_puae_brk;   // r1229: начало арены
    }
    size_t need = PUAE_HDR_SIZE + (((uintptr_t)sz + 15u) & ~(uintptr_t)15u);

    // r1234: сначала ищем подходящий освобождённый блок (first-fit).
    unsigned guard = 0;
    for (puae_blk_t* b = g_puae_free; b; b = *(puae_blk_t**)PUAE_PAY(b)) {
        if (++guard > PUAE_WALK_MAX) { g_puae_free = 0; break; }
        if (b->magic != PUAE_BLK_MAGIC || b->busy || b->total < need) continue;
        puae_blk_unlink(b);
        if (b->total - need >= PUAE_MIN_SPLIT) {
            puae_blk_t* rest = (puae_blk_t*)((char*)b + need);
            rest->magic = PUAE_BLK_MAGIC;
            rest->busy = 0;
            rest->size = 0;
            rest->total = b->total - need;
            b->total = need;
            puae_blk_push(rest);
        }
        b->busy = 1;
        b->size = sz;
        return PUAE_PAY(b);
    }

    char* p = g_puae_brk;
    uintptr_t a = ((uintptr_t)p + 15u) & ~(uintptr_t)15u;
    if (_sbrk((int)(a - (uintptr_t)p + need)) == (void*)-1) return 0;
    puae_blk_t* b = (puae_blk_t*)a;
    b->magic = PUAE_BLK_MAGIC;
    b->busy = 1;
    b->size = sz;
    b->total = need;
    g_puae_brk = (char*)(a + need);
    // r1199: защита от ухода brk из sbrk-зоны (0x4BF00000..0x4EF00000).
    // Если brk вне диапазона — это следствие порчи указателя, а не нехватки
    // памяти: печатаем и отказываем.
    if ((uintptr_t)g_puae_brk < 0x4BF00000u || (uintptr_t)g_puae_brk > 0x4EF00000u) {
        printf("PUAE: brk OUT %08X (sz=%u)\n",
               (unsigned)(uintptr_t)g_puae_brk, (unsigned)sz);
        g_puae_brk = (char*)(uintptr_t)(-1);
        return 0;
    }
    return PUAE_PAY(b);
}

void puae_free(void* p)
{
    if (!p) return;
    // r1229/r1234: освобождаем только ЖИВЫЕ блоки текущей арены. Указатели
    // прошлой сессии и мусорные значения отсекаются проверкой заголовка:
    // без неё free откатывал brk под живые данные (стенд r1228).
    if (!g_puae_base) return;
    puae_blk_t* b = (puae_blk_t*)((char*)p - PUAE_HDR_SIZE);
    if ((char*)b < g_puae_base || (char*)b + PUAE_HDR_SIZE > g_puae_brk) return;
    if (b->magic != PUAE_BLK_MAGIC || !b->busy) return;
    b->busy = 0;

    // Слияние со следующим блоком, если он свободен и внутри арены.
    puae_blk_t* nx = (puae_blk_t*)((char*)b + b->total);
    if ((char*)nx + PUAE_HDR_SIZE <= g_puae_brk &&
        nx->magic == PUAE_BLK_MAGIC && !nx->busy) {
        puae_blk_unlink(nx);
        b->total += nx->total;
    }

    // Хвост арены отдаём назад через brk — иначе арена не сжимается вообще.
    if ((char*)b + b->total == g_puae_brk) {
        g_puae_brk = (char*)b;
        if (g_puae_brk < g_puae_brk_low) g_puae_brk = g_puae_brk_low;
        return;
    }
    puae_blk_push(b);
}

void* puae_calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void* p = puae_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* puae_realloc(void* p, size_t sz)
{
    if (!p) return puae_malloc(sz);
    puae_blk_t* b = (puae_blk_t*)((char*)p - PUAE_HDR_SIZE);
    if (b->magic != PUAE_BLK_MAGIC) return puae_malloc(sz);
    size_t old = b->size;
    if (old >= sz) return p;           // shrink — оставляем как есть
    void* np = puae_malloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    puae_free(p);
    return np;
}

// PUAE-версии strdup/strndup: глобальный newlib-strdup аллоцировал бы через
// gb-пул; здесь — через puae_malloc (та же sbrk-зона).
char* puae_strdup(const char* s)
{
    if (!s) return 0;
    size_t n = strlen(s) + 1;
    char* p = (char*)puae_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char* puae_strndup(const char* s, size_t n)
{
    if (!s) return 0;
    size_t l = 0;
    while (l < n && s[l]) l++;
    char* p = (char*)puae_malloc(l + 1);
    if (p) { memcpy(p, s, l); p[l] = 0; }
    return p;
}

// ---- точка входа emu.c (как c64/fuse) ----
void emu_run_amiga_wb(void)
{
    extern const unsigned char puae_wb13_adf[], puae_wb13_adf_end[];
    uint32_t n = (uint32_t)(puae_wb13_adf_end - puae_wb13_adf);
    printf("Amiga WB: вшитый диск %u байт\n", (unsigned)n);
    emu_run_amiga(puae_wb13_adf, n, "wb13.adf");
}

void emu_run_amiga(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    // r1235: каждый вход — холодный старт. Ядро кэширует адреса прошлой
    // сессии в статиках (init_row_map и др.): при совпадении адресов оно
    // пропускает переинициализацию и работает по мусору (стенд r1230:
    // xlinebuffer = -1 → Data Abort в pfield_do_fill_line). Поэтому арена
    // зануляется и сдвигается на слот прокрутки — адреса сессий различаются.
    if (g_core_inited) {
        puae_retro_unload_game();
        puae_retro_deinit();
        g_core_inited = 0;
    }
    emu_prepare();
    puae_heap_reset();
    {
        size_t gap = (size_t)g_rot_slot * PUAE_ROT_STEP;
        if (gap) (void)puae_malloc(gap);   // резерв-сдвиг, живёт до конца сессии
        printf("Amiga: cold start, arena slot %u (gap=%u КБ)\n",
               g_rot_slot, (unsigned)(gap >> 10));
        g_rot_slot = (g_rot_slot + 1u) % PUAE_ROT_SLOTS;
    }
    // r1198: проверка карты sbrk-зоны ДО первого выделения PUAE (сводка;
    // при провале — номер первой плохой секции). Ловит разъехавшуюся карту MMU.
    {
        volatile uint32_t* p;
        unsigned bad = 0, bad_mb = 0, n = 0;
        for (p = (volatile uint32_t*)0x4BF00000u;
             p < (volatile uint32_t*)0x4EF00000u;
             p += 0x100000u / sizeof(*p), n++) {
            uint32_t save = *p;
            *p = 0xA5A5A5A5u;
            if (*p != 0xA5A5A5A5u) { bad++; if (!bad_mb) bad_mb = n; }
            *p = save;
        }
        printf("sbrk-map: %u/%u OK%s\n", n - bad, n, bad ? " (BAD!)" : "");
        if (bad) printf("sbrk-map: first bad MB=%u\n", bad_mb);
    }
    snd_manifest("amiga", "paula");
    // r1236: PAL-кадр Amiga — 50 Гц (20 мс). Без этого throttle целится в
    // 16667 мкс от emu_prepare и при быстром ядре догонял бы 60 Гц.
    emu_set_period_us(20000u);

    if (rom && rom_name && rom_name[0]) {
        size_t n = strlen(rom_name);
        if (n >= sizeof(g_path)) n = sizeof(g_path) - 1;
        memcpy(g_path, rom_name, n);
        g_path[n] = 0;
    } else if (rom) {
        strcpy(g_path, "game.adf");
    } else {
        g_path[0] = 0;
    }

    // Расширение → нижний регистр (как c64 r1003): PUAE определяет тип
    // образа по расширению файла (.adf), на флешке может быть .ADF.
    {
        char* dot = 0;
        for (char* p = g_path; *p; p++)
            if (*p == '.') dot = p;
        if (dot) {
            for (char* p = dot + 1; *p; p++)
                if (*p >= 'A' && *p <= 'Z') *p += 32;
        }
    }

    // Регистрируем ADF в едином файловом слое (fs_layer/a8_fs.o).
    extern void a8_fs_register(const char* path, const void* data, uint32_t size);
    a8_fs_register(g_path, rom, size);

    // Вшитый Kickstart (kickstart_data.S, bios/kick34005.A500). Регистрируем
    // вторым файлом слоя: ядро читает его при старте тем же fopen/stat, что и
    // образ, поэтому оба буфера должны быть живы одновременно.
    {
        extern void a8_fs_register_bios(const char* path, const void* data, uint32_t size);
        extern const unsigned char puae_kick13_rom[], puae_kick13_rom_end[];
        a8_fs_register_bios("kick34005.A500", puae_kick13_rom,
                            (uint32_t)(puae_kick13_rom_end - puae_kick13_rom));
    }

    // Проба файлового слоя: ядро монтирует контент как диск только если
    // path_is_valid() (через stat) подтверждает файл. Одна строка на сессию.
    {
        struct stat st;
        int st_ok = (stat(g_path, &st) == 0);
        printf("Amiga fs: stat=%d size=%u\n", st_ok,
               (unsigned)(st_ok ? st.st_size : 0));
    }

    puae_retro_set_environment(host_environment);
    puae_retro_set_video_refresh(host_video);
    puae_retro_set_audio_sample(host_audio_sample);
    puae_retro_set_audio_sample_batch(host_audio_sample_batch);
    puae_retro_set_input_poll(host_input_poll);
    puae_retro_set_input_state(host_input_state);

    // Логи ядра: порог INFO живёт, пока не израсходован бюджет строк (старт
    // ядра: KS ver, Known ROM, Disk inserted), затем сам возвращается на WARN.
    g_core_log_min = RETRO_LOG_INFO;
    g_diag_budget = 60;
    g_geo_printed = 0;

    // r1235: вход всегда холодный (см. блок прокрутки арены выше) — ядро
    // инициализируется с нуля, статики не несут мусора прошлой сессии.
    printf("Amiga: init...\n");
    puae_retro_init();
    g_core_inited = 1;
    printf("Amiga: init ok\n");

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = g_path;
    info.data = (void*)rom;
    info.size = size;

    printf("Amiga: load...\n");
    if (!puae_retro_load_game(&info)) {
        printf("Amiga: load failed\n");
        puae_retro_unload_game();
        return;
    }
    // r1234: НЕ выдёргиваем диск, вставленный ядром при load_game. Ручной
    // eject→insert переводит привод в «диск сменился» уже ПОСЛЕ первого
    // прохода m68k_go, и Kickstart остаётся на экране «вставьте диск» (стенд
    // r1233: и Workbench 1.3, и игровой ADF ведут себя одинаково). Ядро само
    // пишет «Disk (1) inserted in drive DF0» — этого достаточно. Ручной путь
    // остаётся только как страховка, если индекс образа ядро не приняло.
    extern bool puae_retro_disk_set_image_index(unsigned index);
    extern bool puae_retro_disk_set_eject_state(bool ejected);
    int disk_mounted;
    if (puae_retro_disk_set_image_index(0)) {
        disk_mounted = 1;
        printf("Amiga disk: mount=1 (контент смонтирован ядром)\n");
    } else {
        puae_retro_disk_set_eject_state(true);
        disk_mounted = (int)puae_retro_disk_set_eject_state(false);
        printf("Amiga disk: mount=%d (ручная вставка)\n", disk_mounted);
    }

    printf("Amiga: started (%s, %u bytes)\n", g_path[0] ? g_path : "AROS/BIOS", (unsigned)size);

    emu_set_border_color(0x00080814);   // тёмно-синий
    emu_throttle_reset();
    emu_esc_hold_reset();
    // Замер: сколько времени уходит на ядро и сколько на видео (скейл+flush).
    // Итог печатается на выходе — по нему видно, что именно тормозит.
    uint32_t t_core = 0, t_video = 0, nframes = 0;
    int remount_done = 0;
    for (;;) {
        uint32_t t0 = h3_hs_timer_lo_us();
        puae_retro_run();          // кадр PUAE -> host_video -> emu_scale_rgb565_src
        uint32_t t1 = h3_hs_timer_lo_us();
        // Ядро применяет часть настроек (и вставку диска) только когда прогон
        // уже идёт (libretro_runloop_active). Повторяем вставку один раз и
        // ТОЛЬКО если первая не удалась: лишний eject→insert во время загрузки
        // поднимает DSKCHANGE, Kickstart перезапускает boot и уходит в цикл
        // (стенд r1227: диск читается, но на экране «рука с дискетой»).
        if (!remount_done) {
            remount_done = 1;
            if (!disk_mounted)
                printf("Amiga disk retry: mount=%d\n",
                       (int)puae_retro_disk_set_eject_state(false));
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
    if (nframes) {
        printf("Amiga disk reads=%u bytes=%u opens=%u\n",
               g_fs_read_ops, g_fs_read_bytes, g_fs_rom_opens);
        printf("Amiga perf: fps=%.1f core=%.1fms video=%.1fms\n",
               (double)nframes * 1000000.0 / (double)(t_core + t_video),
               (double)t_core / (double)nframes / 1000.0,
               (double)t_video / (double)nframes / 1000.0);
    }
    // r1235: закрываем сессию полностью — deinit сносит состояние ядра, а
    // следующий вход идёт холодным путём (арена зануляется и прокручивается).
    puae_retro_unload_game();
    puae_retro_deinit();
    g_core_inited = 0;
    printf("Amiga heap: base=%08X brk=%08X\n",
           (unsigned)(uintptr_t)g_puae_base, (unsigned)(uintptr_t)g_puae_brk);
    emu_audio_stop();
    fb_clear();
    fb_flush();
}